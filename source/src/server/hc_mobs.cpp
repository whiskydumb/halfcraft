// server.dll: minecraft's mobs among half-life's characters (see hc_mobs.h).

#include "cbase.h"
#include "ai_interactions.h"
#include "npc_bullseye.h"
#include "takedamageinfo.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/hc_log.h"
#include "core/hc_units.h"
#include "server/hc_combat.h"
#include "server/hc_mobs.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
	constexpr char MOB_CLASSNAME[] = "halfcraft_mob";
	// half-life's health per point of minecraft's (as for the player, whose damage minecraft divides by 5)
	constexpr float HOST_HEALTH_PER_MC = 5.0f;
	// where half-life's characters look and aim on a stand-in, from its feet
	constexpr float EYE_FRACTION = 0.85f;
	// seconds between two "hurt minecraft's mob" lines for one mob: an smg hurts it once a bullet
	constexpr float HURT_LOG_INTERVAL = 1.0f;
}

// one of minecraft's mobs as half-life's characters see it (see hc_mobs.h)
class CHalfCraftMob : public CNPC_Bullseye
{
public:
	DECLARE_CLASS(CHalfCraftMob, CNPC_Bullseye);

	void    Spawn() override;
	Class_T Classify() override;
	int     OnTakeDamage(const CTakeDamageInfo& info) override;
	// it exists while minecraft lists its mob: never saved, never carried into the next level
	int ObjectCaps() override { return (BaseClass::ObjectCaps() | FCAP_DONT_SAVE) & ~FCAP_ACROSS_TRANSITION; }
	// minecraft burns its own mobs: half-life's fire would burn them twice and draw flames on nothing
	void Ignite(float, bool, float, bool) override {}
	// a barnacle hates monsters and the player's allies, but it can't lift a stand-in: it would hang a
	// ragdoll of the stand-in's model from its tongue, and a stand-in has none (a null ragdoll crashes it)
	bool HandleInteraction(int interaction_type, void* data, CBaseCombatCharacter* source) override;
	// the player's own traces (+use and movement: the masks with playerclip) look through a stand-in, so a
	// pet that follows close behind doesn't hide buttons and pickups; weapons' traces still hit it
	bool ShouldCollide(int collision_group, int contents_mask) const override;

	/// takes on minecraft's latest record of its mob: place, size, facing, health, who it is to npcs.
	void follow(const halfcraft::proto::MobRecord& record, halfcraft::MapSlot slot);

private:
	std::uint32_t mc_id_ = 0;
	std::uint32_t flags_ = 0;
	float         next_hurt_log_ = 0.0f;
	int           hurts_since_log_ = 0;
};

LINK_ENTITY_TO_CLASS(halfcraft_mob, CHalfCraftMob);

void CHalfCraftMob::Spawn()
{
	BaseClass::Spawn();
	// bullets, blasts and blows hit it; nothing bumps into it (the player, npcs, props)
	SetCollisionGroup(COLLISION_GROUP_WEAPON);
	SetBloodColor(BLOOD_COLOR_RED);
}

Class_T CHalfCraftMob::Classify()
{
	return (flags_ & halfcraft::proto::kMobPet) ? CLASS_PLAYER_ALLY : CLASS_ZOMBIE;
}

bool CHalfCraftMob::HandleInteraction(int interaction_type, void* data, CBaseCombatCharacter* source)
{
	if (interaction_type == g_interactionBarnacleVictimGrab) {
		return false;  // the barnacle lets go of it before it grabs
	}
	return BaseClass::HandleInteraction(interaction_type, data, source);
}

bool CHalfCraftMob::ShouldCollide(int collision_group, int contents_mask) const
{
	return !(contents_mask & CONTENTS_PLAYERCLIP) && BaseClass::ShouldCollide(collision_group, contents_mask);
}

void CHalfCraftMob::follow(const halfcraft::proto::MobRecord& record, halfcraft::MapSlot slot)
{
	using namespace halfcraft;
	mc_id_ = record.id;
	flags_ = record.flags;
	const float  units = static_cast<float>(UNITS_PER_BLOCK);
	const float  half = record.width * 0.5f * units;
	const float  tall = record.height * units;
	const Vector mins(-half, -half, 0.0f), maxs(half, half, tall);
	if (CollisionProp()->OBBMins() != mins || CollisionProp()->OBBMaxs() != maxs) {
		UTIL_SetSize(this, mins, maxs);
		SetViewOffset(Vector(0.0f, 0.0f, tall * EYE_FRACTION));
	}
	float feet[3];
	mc_to_source(record.x, record.y, record.z, slot, feet);
	UTIL_SetOrigin(this, Vector(feet[0], feet[1], feet[2]));
	SetAbsAngles(QAngle(0.0f, mc_yaw_to_source(record.yaw), 0.0f));
	m_iMaxHealth = std::max(1, static_cast<int>(std::lround(record.maxHealth * HOST_HEALTH_PER_MC)));
	m_iHealth = std::max(1, static_cast<int>(std::lround(record.health * HOST_HEALTH_PER_MC)));
}

int CHalfCraftMob::OnTakeDamage(const CTakeDamageInfo& info)
{
	using namespace halfcraft;
	CBaseEntity* attacker = info.GetAttacker();
	if (info.GetDamage() <= 0.0f || !attacker) {
		return 0;
	}
	// the world's hurts are minecraft's own fire, lava and magma (hc_hazards): minecraft already did
	// those to its mob. so are minecraft's explosions, but their half-life blast spares stand-ins (hc_blast)
	if (attacker->IsWorld()) {
		return 0;
	}
	const PushInputFn push_input = client_push_input();
	if (!push_input) {
		return 0;
	}
	const std::uint32_t attacker_id = attacker->IsPlayer() ? proto::kMobAttackerPlayer : host_actor_id(attacker);
	push_input(proto::kInHurtMob, hurt_kind(info.GetDamageType()), static_cast<int>(mc_id_), static_cast<int>(std::lround(info.GetDamage() * 100.0f)),
		static_cast<int>(attacker_id));
	++hurts_since_log_;
	if (gpGlobals->curtime >= next_hurt_log_) {
		log_info("%s hurt minecraft's mob %u for %.1f (hurts since the last line: %d)", attacker->GetClassname(), mc_id_, info.GetDamage(), hurts_since_log_);
		hurts_since_log_ = 0;
		next_hurt_log_ = gpGlobals->curtime + HURT_LOG_INTERVAL;
	}
	return static_cast<int>(info.GetDamage());
}

namespace halfcraft
{
	namespace
	{
		// keeps stand-ins this far inside source's world bounds (a mob listed from another map's slot is out)
		constexpr float WORLD_MARGIN = 64.0f;

		struct StandIn
		{
			EHANDLE       entity;
			std::uint32_t frame = 0;
		};

		// minecraft entity id -> its stand-in
		std::unordered_map<std::uint32_t, StandIn> g_stand_ins;

		bool inside_world(const float p[3])
		{
			const float limit = MAX_COORD_FLOAT - WORLD_MARGIN;
			return std::fabs(p[0]) < limit && std::fabs(p[1]) < limit && std::fabs(p[2]) < limit;
		}

		CHalfCraftMob* spawn_stand_in()
		{
			auto* mob = static_cast<CHalfCraftMob*>(CreateEntityByName(MOB_CLASSNAME));
			if (!mob) {
				log_error("couldn't create %s", MOB_CLASSNAME);
				return nullptr;
			}
			DispatchSpawn(mob);
			return mob;
		}

		void remove_all()
		{
			for (auto& [id, stand_in] : g_stand_ins) {
				if (CBaseEntity* entity = stand_in.entity.Get()) {
					UTIL_Remove(entity);
				}
			}
			g_stand_ins.clear();
		}
	}

	CBaseEntity* mob_stand_in(std::uint32_t mc_id)
	{
		const auto found = g_stand_ins.find(mc_id);
		return found != g_stand_ins.end() ? found->second.entity.Get() : nullptr;
	}

	bool is_mob_stand_in(CBaseEntity* entity)
	{
		return entity && FClassnameIs(entity, MOB_CLASSNAME);
	}

	void Mobs::update(Link& link, MapSlot slot)
	{
		if (!link.mc_alive()) {
			remove_all();
			return;
		}
		if (!link.read_mobs(table_)) {
			return;  // minecraft was writing: last frame's stand-ins stay a frame longer
		}
		++frame_;
		int hostile = 0, pets = 0;
		for (std::uint32_t i = 0; i < table_.count; ++i) {
			const proto::MobRecord& record = table_.mobs[i];
			float                   feet[3];
			mc_to_source(record.x, record.y, record.z, slot, feet);
			if (!(record.flags & (proto::kMobHostile | proto::kMobPet)) || !inside_world(feet)) {
				continue;
			}
			StandIn& stand_in = g_stand_ins[record.id];
			auto*    mob = static_cast<CHalfCraftMob*>(stand_in.entity.Get());
			if (!mob) {
				mob = spawn_stand_in();
				if (!mob) {
					g_stand_ins.erase(record.id);
					continue;
				}
				stand_in.entity = mob;
			}
			mob->follow(record, slot);
			stand_in.frame = frame_;
			if (record.flags & proto::kMobPet) {
				++pets;
			} else {
				++hostile;
			}
		}
		for (auto it = g_stand_ins.begin(); it != g_stand_ins.end();) {
			if (it->second.frame != frame_) {
				if (CBaseEntity* entity = it->second.entity.Get()) {
					UTIL_Remove(entity);
				}
				it = g_stand_ins.erase(it);
			} else {
				++it;
			}
		}
		// a line when the count changes, but not for every mob walking in and out of range
		const std::size_t count = g_stand_ins.size();
		if (count != logged_ && (count % 5 == 0 || count < 5)) {
			log_info("%zu of minecraft's mobs stand in among half-life's characters (%d monsters, %d pets)", count, hostile, pets);
		}
		logged_ = count;
	}

	void Mobs::reset()
	{
		// the level's entities go with it: only forget them
		g_stand_ins.clear();
		logged_ = 0;
	}
}

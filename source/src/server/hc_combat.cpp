// server.dll: fighting across the two games (see hc_combat.h).

#include "cbase.h"
#include "ai_basenpc.h"
#include "explode.h"
#include "player.h"
#include "takedamageinfo.h"
#include "world.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "core/hc_log.h"
#include "core/hc_module.h"
#include "core/hc_units.h"
#include "server/hc_combat.h"
#include "server/hc_mobs.h"
#include "server/hc_vitals.h"
#include "shared/hc_hooks.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		ConVar hc_damage_to_npc("hc_damage_to_npc", "3", FCVAR_ARCHIVE,
			"halfcraft: half-life damage per point of minecraft damage (a diamond sword's 7 -> 21; a metrocop has 40 health)");
		ConVar hc_explosion_damage("hc_explosion_damage", "30", FCVAR_ARCHIVE, "halfcraft: half-life blast damage per block of a minecraft explosion's radius");

		constexpr float NPC_RANGE = 64.0f * static_cast<float>(UNITS_PER_BLOCK);      // arrows fly far
		constexpr float PROP_RANGE = 24.0f * static_cast<float>(UNITS_PER_BLOCK);     // breakables only need reach and a bow shot
		constexpr float KNOCKBACK_UNITS = 300.0f;  // source velocity per point of minecraft knockback
		constexpr float MAX_KNOCKBACK = 1.5f;      // a knockback ii sword with a sprint hit
		constexpr int   MAX_NEARBY = 512;

		// what to call half-life's npcs over a minecraft stand-in (the rest lose their "npc_")
		constexpr const char* NPC_NAMES[][2] = {
			{ "npc_metropolice", "Civil Protection" }, { "npc_combine_s", "Combine Soldier" }, { "npc_zombie", "Zombie" },
			{ "npc_fastzombie", "Fast Zombie" }, { "npc_poisonzombie", "Poison Zombie" }, { "npc_zombine", "Zombine" },
			{ "npc_headcrab", "Headcrab" }, { "npc_headcrab_fast", "Fast Headcrab" }, { "npc_headcrab_black", "Poison Headcrab" },
			{ "npc_headcrab_poison", "Poison Headcrab" }, { "npc_antlion", "Antlion" }, { "npc_antlionguard", "Antlion Guard" },
			{ "npc_manhack", "Manhack" }, { "npc_cscanner", "City Scanner" }, { "npc_clawscanner", "Shield Scanner" },
			{ "npc_rollermine", "Rollermine" }, { "npc_turret_floor", "Turret" }, { "npc_combinegunship", "Gunship" },
			{ "npc_helicopter", "Hunter-Chopper" }, { "npc_strider", "Strider" }, { "npc_barnacle", "Barnacle" },
			{ "npc_citizen", "Citizen" }, { "npc_alyx", "Alyx" }, { "npc_barney", "Barney" }, { "npc_vortigaunt", "Vortigaunt" },
			{ "npc_dog", "Dog" }, { "npc_eli", "Eli" }, { "npc_kleiner", "Dr. Kleiner" }, { "npc_mossman", "Dr. Mossman" },
			{ "npc_breen", "Dr. Breen" }, { "npc_monk", "Father Grigori" }, { "npc_gman", "G-Man" }, { "npc_stalker", "Stalker" },
			{ "npc_hunter", "Hunter" }, { "npc_crow", "Crow" }, { "npc_pigeon", "Pigeon" }, { "npc_seagull", "Seagull" },
		};

		/// breakable things minecraft weapons should be able to smash
		bool is_breakable(CBaseEntity* entity)
		{
			if (!entity || entity->IsPlayer() || entity->MyNPCPointer() || entity->m_takedamage != DAMAGE_YES || entity->GetHealth() <= 0) {
				return false;
			}
			if (entity->IsEffectActive(EF_NODRAW) || !entity->IsSolid()) {
				return false;
			}
			const char* name = entity->GetClassname();
			return !Q_strncmp(name, "prop_", 5) || !Q_strcmp(name, "item_item_crate") || !Q_strcmp(name, "func_breakable") ||
				   !Q_strcmp(name, "func_physbox");
		}

		void display_name(CBaseEntity* entity, char* out, std::size_t size)
		{
			const char* classname = entity->GetClassname();
			for (const auto& entry : NPC_NAMES) {
				if (!Q_stricmp(classname, entry[0])) {
					Q_strncpy(out, entry[1], static_cast<int>(size));
					return;
				}
			}
			Q_strncpy(out, !Q_strncmp(classname, "npc_", 4) ? classname + 4 : classname, static_cast<int>(size));
		}

		bool g_killing_player = false;  // minecraft's own death: let source's damage through

		// actor id -> entity, as of the last table sent (events refer to it)
		std::unordered_map<std::uint32_t, EHANDLE> g_actors;

		std::uint32_t actor_id(CBaseEntity* entity)
		{
			return static_cast<std::uint32_t>(entity->GetRefEHandle().ToInt());
		}

		/// minecraft's screenshot key: client.dll draws the frames, so it reads one back (hc_bridge.h)
		void request_screenshot(const proto::McEvent& event)
		{
			static RequestScreenshotFn request = nullptr;
			if (!request) {
				request = reinterpret_cast<RequestScreenshotFn>(find_export("client.dll", HC_REQUEST_SCREENSHOT_EXPORT));
				if (!request) {
					log_warning("minecraft asked for screenshot %u, but client.dll has no %s", event.actorId, HC_REQUEST_SCREENSHOT_EXPORT);
					return;
				}
			}
			request(event.actorId);
		}
	}

	void Combat::update(Link& link, CBasePlayer* player, MapSlot slot, bool puppeted)
	{
		if (!player || !puppeted) {
			// nothing to fight while source has the player; still drain minecraft's events so stale hits
			// don't land later, but its death still counts
			proto::McEvent event;
			while (link.pop_event(event)) {
				if (event.type == proto::kEvScreenshot) {
					request_screenshot(event);  // a frame is a frame, whoever has the player
				}
				if (event.type == proto::kEvPlayerDied && player && player->IsAlive() && link.mc_alive()) {
					g_killing_player = true;
					player->TakeDamage(CTakeDamageInfo(GetWorldEntity(), GetWorldEntity(), player->GetHealth() + 100.0f, DMG_GENERIC));
					g_killing_player = false;
				}
			}
			if (actors_sent_) {
				link.write_actors(nullptr, 0);
				actors_sent_ = false;
			}
			g_actors.clear();
			return;
		}

		write_actors(link, player, slot);

		proto::McEvent event;
		while (link.pop_event(event)) {
			switch (event.type) {
			case proto::kEvHitActor:
				apply_hit(player, event);
				break;
			case proto::kEvPlayerDied:
				if (player->IsAlive()) {
					log_info("minecraft's player died: so does gordon");
					g_killing_player = true;
					player->TakeDamage(CTakeDamageInfo(GetWorldEntity(), GetWorldEntity(), player->GetHealth() + 100.0f, DMG_GENERIC));
					g_killing_player = false;
				}
				break;
			case proto::kEvExplosion:
				apply_explosion(player, event, slot);
				break;
			case proto::kEvArrowStuck:
				stick_arrow(event, slot);
				break;
			case proto::kEvScreenshot:
				request_screenshot(event);
				break;
			default:
				break;
			}
		}
	}

	void Combat::write_actors(Link& link, CBasePlayer* player, MapSlot slot)
	{
		records_.clear();
		g_actors.clear();
		const Vector& eye = player->GetAbsOrigin();

		auto add = [&](CBaseEntity* entity, std::uint32_t flags, bool named) {
			if (records_.size() >= proto::kMaxActors) {
				return;
			}
			Vector mins, maxs;
			entity->CollisionProp()->WorldSpaceAABB(&mins, &maxs);
			const float feet[3] = { (mins.x + maxs.x) * 0.5f, (mins.y + maxs.y) * 0.5f, mins.z };
			const auto  mc = source_to_mc(feet, slot);
			const float units = static_cast<float>(UNITS_PER_BLOCK);

			proto::ActorRecord r{};
			r.id = actor_id(entity);
			r.flags = flags;
			r.x = static_cast<float>(mc.x);
			r.y = static_cast<float>(mc.y);
			r.z = static_cast<float>(mc.z);
			r.yaw = source_yaw_to_mc(entity->GetAbsAngles().y);
			r.width = std::clamp(std::max(maxs.x - mins.x, maxs.y - mins.y) / units, 0.3f, 6.0f);
			r.height = std::clamp((maxs.z - mins.z) / units, 0.3f, 12.0f);
			r.healthFrac = entity->GetMaxHealth() > 0 ? std::clamp(static_cast<float>(entity->GetHealth()) / entity->GetMaxHealth(), 0.0f, 1.0f) : 1.0f;
			r.level = 1;
			if (named) {
				display_name(entity, r.name, sizeof(r.name));
			}
			records_.push_back(r);
			g_actors[r.id] = entity;
		};

		// npcs
		CAI_BaseNPC** npcs = g_AI_Manager.AccessAIs();
		for (int i = 0; i < g_AI_Manager.NumAIs(); ++i) {
			CAI_BaseNPC* npc = npcs[i];
			if (!npc || !npc->IsAlive() || npc->IsEffectActive(EF_NODRAW) || npc->m_takedamage == DAMAGE_NO) {
				continue;
			}
			if ((npc->GetAbsOrigin() - eye).LengthSqr() > NPC_RANGE * NPC_RANGE) {
				continue;
			}
			const std::uint32_t flags = (npc->IRelationType(player) == D_HT ? proto::kActorHostile : 0u) | (npc->GetEnemy() == player ? proto::kActorInCombat : 0u);
			add(npc, flags, true);
		}

		// crates, breakable props and glass
		CBaseEntity* nearby[MAX_NEARBY];
		const Vector reach(PROP_RANGE, PROP_RANGE, PROP_RANGE);
		const int    count = UTIL_EntitiesInBox(nearby, MAX_NEARBY, eye - reach, eye + reach, 0);
		for (int i = 0; i < count; ++i) {
			if (is_breakable(nearby[i])) {
				add(nearby[i], 0u, false);
			}
		}

		link.write_actors(records_.data(), static_cast<std::uint32_t>(records_.size()));
		actors_sent_ = true;
	}

	void Combat::apply_hit(CBasePlayer* player, const proto::McEvent& event)
	{
		const auto   found = g_actors.find(event.actorId);
		CBaseEntity* target = found != g_actors.end() ? found->second.Get() : nullptr;
		if (!target || target->m_takedamage == DAMAGE_NO) {
			return;
		}

		// what minecraft hit with decides how half-life takes it
		int type;
		switch (event.weapon) {
		case proto::kWeaponBlade:
		case proto::kWeaponAxe:
		case proto::kWeaponPierce:
			type = DMG_SLASH;
			break;
		case proto::kWeaponArrow:
			type = DMG_BULLET | DMG_NEVERGIB;
			break;
		default:
			type = DMG_CLUB;
			break;
		}
		if ((event.flags & proto::kHitProjectile) && event.weapon != proto::kWeaponArrow) {
			type = DMG_BULLET;  // tridents, snowballs, ...
		}
		const float damage = event.a * std::max(0.0f, hc_damage_to_npc.GetFloat());

		// a minecraft mob's hit comes from its stand-in, so the character fights that mob; one with no
		// stand-in (an animal) blames nobody, and only the player's own hits are the player's
		CBaseEntity* stand_in = event.attackerId != 0 ? mob_stand_in(event.attackerId) : nullptr;
		CBaseEntity* attacker = event.attackerId == 0 ? player : stand_in ? stand_in : GetWorldEntity();
		if (event.attackerId != 0) {
			log_info("minecraft's mob %u hit %s for %.1f%s", event.attackerId, target->GetClassname(), damage, stand_in ? "" : " (it has no stand-in: nobody to blame)");
		}

		// a weapon's trace from the attacker, so blood, decals and hit reactions come out right
		CBaseEntity* from = stand_in ? stand_in : player;
		const Vector eye = stand_in ? stand_in->WorldSpaceCenter() : player->EyePosition();
		const Vector centre = target->WorldSpaceCenter();
		Vector       dir = centre - eye;
		VectorNormalize(dir);
		trace_t tr;
		UTIL_TraceLine(eye, centre + dir * 16.0f, MASK_SHOT, from, COLLISION_GROUP_NONE, &tr);
		if (tr.m_pEnt != target) {
			tr.m_pEnt = target;
			tr.endpos = centre;
			tr.fraction = 0.5f;
			tr.hitgroup = HITGROUP_GENERIC;
			tr.plane.normal = -dir;
		}

		CTakeDamageInfo info(attacker, attacker, damage, type);
		info.SetDamagePosition(tr.endpos);
		// minecraft's knockback (its direction, in minecraft's x/z) pushes physics props and ragdolls
		Vector push = dir;
		if (event.d > 0.0f) {
			push.Init(event.b, -event.c, 0.0f);
			VectorNormalize(push);
		}
		CalculateMeleeDamageForce(&info, push, tr.endpos, 1.0f + event.d);

		ClearMultiDamage();
		target->DispatchTraceAttack(info, dir, &tr);
		ApplyMultiDamage();

		if ((event.flags & proto::kHitFire) && target->IsAlive()) {
			if (CBaseAnimating* animating = target->GetBaseAnimating()) {
				animating->Ignite(4.0f);  // fire aspect, flame arrows
			}
		}
		// and shoves the enemies it hurts. not friends (half-life ignores their player's hits, so they'd
		// only get the push), nor anyone in a scripted scene or off the ground: the scene would lose them,
		// and no gravity pulls a scripted npc back down
		CAI_BaseNPC* npc = target->MyNPCPointer();
		if (event.d > 0.0f && npc && npc->IsAlive() && npc->IRelationType(player) == D_HT && !npc->IsInAScript() &&
			npc->GetMoveType() == MOVETYPE_STEP && (npc->GetFlags() & FL_ONGROUND)) {
			npc->ApplyAbsVelocityImpulse(Vector(push.x, push.y, 0.0f) * std::min(event.d, MAX_KNOCKBACK) * KNOCKBACK_UNITS);
		}
	}

	void Combat::apply_explosion(CBasePlayer* player, const proto::McEvent& event, MapSlot slot)
	{
		float centre[3];
		mc_to_source(event.a, event.b, event.c, slot, centre);
		const float   radius = std::max(1.0f, event.d) * static_cast<float>(UNITS_PER_BLOCK);
		const int     magnitude = static_cast<int>(std::max(1.0f, event.d) * std::max(0.0f, hc_explosion_damage.GetFloat()));
		const EHANDLE ignore = player;  // minecraft already hurt its own player
		// half-life's own fireball, blast damage and push; minecraft makes the sound
		ExplosionCreate(Vector(centre[0], centre[1], centre[2]), vec3_angle, player, magnitude, static_cast<int>(radius), true, &ignore, CLASS_NONE, 0.0f, false,
			true);
	}

	void Combat::stick_arrow(const proto::McEvent& event, MapSlot slot)
	{
		const auto   found = g_actors.find(event.actorId);
		CBaseEntity* target = found != g_actors.end() ? found->second.Get() : nullptr;
		if (!target) {
			return;
		}
		static StickArrowFn stick = nullptr;
		if (!stick) {
			stick = reinterpret_cast<StickArrowFn>(find_export("client.dll", HC_STICK_ARROW_EXPORT));
			if (!stick) {
				return;
			}
		}
		float hit[3];
		mc_to_source(event.a, event.b, event.c, slot, hit);
		// minecraft's arrows fly along (sin yaw cos pitch, sin pitch, cos yaw cos pitch); the pitch rides in flags
		float pitch_degrees;
		std::memcpy(&pitch_degrees, &event.flags, sizeof(pitch_degrees));
		const float yaw = DEG2RAD(event.d), pitch = DEG2RAD(pitch_degrees);
		const float mc_direction[3] = { std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch) };
		const float direction[3] = { mc_direction[0], -mc_direction[2], mc_direction[1] };
		stick(target->entindex(), hit, direction);
	}

	std::uint32_t host_actor_id(CBaseEntity* entity)
	{
		return actor_id(entity);
	}

	int hurt_kind(int damage_type)
	{
		if (damage_type & (DMG_CLUB | DMG_SLASH)) {
			return proto::kHurtMelee;
		}
		if (damage_type & (DMG_BULLET | DMG_BUCKSHOT)) {
			return proto::kHurtProjectile;
		}
		if (damage_type & (DMG_SHOCK | DMG_ENERGYBEAM | DMG_DISSOLVE | DMG_PLASMA | DMG_RADIATION | DMG_ACID | DMG_POISON | DMG_NERVEGAS)) {
			return proto::kHurtMagic;
		}
		return proto::kHurtOther;
	}

	bool server_player_damage(CBasePlayer* player, const CTakeDamageInfo& info)
	{
		if (g_killing_player || !minecraft_owns_health()) {
			return false;
		}
		const int type = info.GetDamageType();
		if (type & DMG_FALL) {
			return true;  // minecraft takes its own falls
		}
		static PushHurtFn push = nullptr;
		if (!push) {
			push = reinterpret_cast<PushHurtFn>(find_export("client.dll", HC_PUSH_HURT_EXPORT));
			if (!push) {
				return false;  // no bridge: source keeps the hit
			}
		}

		const int           kind = hurt_kind(type);
		CBaseEntity*        attacker = info.GetAttacker();
		const std::uint32_t attacker_id = attacker && attacker != player && g_actors.count(actor_id(attacker)) ? actor_id(attacker) : 0;
		if (info.GetDamage() > 0.0f) {
			push(kind, info.GetDamage(), attacker_id, 0);
		}
		return true;
	}
}

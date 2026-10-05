// server.dll: minecraft's explosions in half-life (see hc_blast.h).

#include "cbase.h"
#include "ai_basenpc.h"
#include "entityoutput.h"
#include "explode.h"
#include "player.h"
#include "world.h"

#include "tier0/valve_minmax_off.h"
#include <utility>
#include <vector>

#include "core/hc_log.h"
#include "server/hc_blast.h"
#include "server/hc_mobs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr int   MAX_NEARBY = 512;
		constexpr float QUERY_SLACK = 16.0f;  // half-life's own sphere query may reach a little further than ours
		constexpr int   LOGGED_NAMES = 6;

		bool g_running = false;  // inside minecraft_blast's own ExplosionCreate

		enum class Spared
		{
			kNo,
			kScripted,  // the map's scripting depends on it
			kAlly,      // the player's friend, safe from the player's blasts
			kStandIn,   // a minecraft mob's stand-in
		};

		/// whether any of its outputs (OnBreak, OnHealthChanged, OnPlayerUse, ...) fires at something.
		bool has_wired_outputs(CBaseEntity* entity)
		{
			for (datamap_t* map = entity->GetDataDescMap(); map; map = map->baseMap) {
				for (int i = 0; i < map->dataNumFields; ++i) {
					const typedescription_t& field = map->dataDesc[i];
					if (field.fieldType != FIELD_CUSTOM || !(field.flags & FTYPEDESC_OUTPUT)) {
						continue;
					}
					auto* output = reinterpret_cast<CBaseEntityOutput*>(reinterpret_cast<char*>(entity) + field.fieldOffset[TD_OFFSET_NORMAL]);
					if (output->NumberOfElements() > 0) {
						return true;
					}
				}
			}
			return false;
		}

		/// what a minecraft explosion leaves alone, and why (see hc_blast.h).
		/// @param nobodys - nobody's blast (tnt): the player's doing, as far as half-life's friends go
		Spared spared(CBaseEntity* entity, bool nobodys, CBasePlayer* player)
		{
			if (!entity || entity->IsPlayer() || entity->IsWorld()) {
				return Spared::kNo;
			}
			if (is_mob_stand_in(entity)) {
				return Spared::kStandIn;
			}
			if (CAI_BaseNPC* npc = entity->MyNPCPointer()) {
				// half-life's own rule for the player's blasts (CAI_BaseNPC::PassesDamageFilter), which a blast
				// blamed on the world wouldn't meet
				const bool ally = nobodys && player && (npc->CapabilitiesGet() & bits_CAP_FRIENDLY_DMG_IMMUNE) && player->IRelationType(npc) == D_LI;
				return ally ? Spared::kAlly : Spared::kNo;
			}
			// only what can break: the unbreakable still get pushed (and fire their damage outputs, as the
			// player's grenades would)
			if (entity->m_takedamage != DAMAGE_YES) {
				return Spared::kNo;
			}
			const bool scripted = entity->GetEntityName() != NULL_STRING || entity->m_hDamageFilter.Get() || has_wired_outputs(entity);
			return scripted ? Spared::kScripted : Spared::kNo;
		}
	}

	void minecraft_blast(const Vector& centre, float radius, int magnitude, CBaseEntity* owner, CBasePlayer* player)
	{
		// what the blast must leave alone takes no damage while it goes off (half-life's radius damage
		// skips those, push and all), then gets its own back
		std::vector<std::pair<EHANDLE, int>> held;
		int                                  scripted = 0;
		int                                  allies = 0;
		int                                  stand_ins = 0;
		char                                 names[256] = "";
		CBaseEntity*                         nearby[MAX_NEARBY];
		const int                            count = UTIL_EntitiesInSphere(nearby, MAX_NEARBY, centre, radius + QUERY_SLACK, 0);
		for (int i = 0; i < count; ++i) {
			CBaseEntity* entity = nearby[i];
			if (entity->m_takedamage == DAMAGE_NO) {
				continue;
			}
			switch (spared(entity, !owner, player)) {
			case Spared::kNo:
				continue;
			case Spared::kScripted:
				if (scripted++ < LOGGED_NAMES) {
					const char* name = STRING(entity->GetEntityName());
					const int   used = Q_strlen(names);
					Q_snprintf(names + used, sizeof(names) - used, " %s%s%s", entity->GetClassname(), *name ? ":" : "", name);
				}
				break;
			case Spared::kAlly:
				++allies;
				break;
			case Spared::kStandIn:
				++stand_ins;
				break;
			}
			held.emplace_back(EHANDLE(entity), entity->m_takedamage.Get());
			entity->m_takedamage = DAMAGE_NO;
		}

		const EHANDLE ignore = player;
		CBaseEntity*  blamed = owner ? owner : GetWorldEntity();
		// half-life's own fireball, blast damage and push; minecraft makes the sound
		g_running = true;
		ExplosionCreate(centre, vec3_angle, blamed, magnitude, static_cast<int>(radius), true, &ignore, CLASS_NONE, 0.0f, false, true);
		g_running = false;

		for (auto& [handle, takedamage] : held) {
			if (CBaseEntity* entity = handle.Get()) {
				entity->m_takedamage = static_cast<char>(takedamage);
			}
		}
		log_info("minecraft's explosion at (%.0f %.0f %.0f), radius %.0f, magnitude %d, blamed on %s; spares %d scripted thing(s)%s%s, %d ally(s), %d stand-in(s)",
			centre.x, centre.y, centre.z, radius, magnitude, owner ? owner->GetClassname() : "nobody", scripted, scripted ? ":" : "", names, allies, stand_ins);
	}

	bool minecraft_blast_running()
	{
		return g_running;
	}
}

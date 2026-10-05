// server.dll: what's under the crosshair, for minecraft's debug screen (see hc_debug_target.h).

#include "cbase.h"
#include "ai_basenpc.h"
#include "ai_schedule.h"
#include "player.h"

#include "tier0/valve_minmax_off.h"

#include "core/hc_log.h"
#include "core/hc_text.h"
#include "server/hc_debug_target.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr double PUBLISH_SECONDS = 0.1;  // minecraft's debug screen needs no more than this
		constexpr float  REACH = 8192.0f;        // units: about as far as a map goes
	}

	void DebugTarget::update(Link& link, CBasePlayer* player)
	{
		const double now = Plat_FloatTime();
		if (now < next_publish_ || !link.mc_alive()) {
			return;
		}
		next_publish_ = now + PUBLISH_SECONDS;

		proto::HostDebugServer debug{};
		debug.entityCount = static_cast<std::uint32_t>(engine->GetEntityCount());
		CBaseEntity* target = nullptr;
		if (player) {
			Vector forward;
			player->EyeVectors(&forward);
			const Vector eye = player->EyePosition();
			trace_t      trace;
			UTIL_TraceLine(eye, eye + forward * REACH, MASK_SHOT, player, COLLISION_GROUP_NONE, &trace);
			if (trace.m_pEnt && !trace.m_pEnt->IsWorld()) {
				target = trace.m_pEnt;
				debug.distance = (trace.endpos - eye).Length();
			}
		}
		CAI_BaseNPC* npc = target ? target->MyNPCPointer() : nullptr;
		if (target) {
			debug.targetIndex = target->entindex();
			debug.health = target->GetHealth();
			debug.maxHealth = target->GetMaxHealth();
			copy_utf8(debug.targetClass, target->GetClassname());
			copy_utf8(debug.targetName, STRING(target->GetEntityName()));
			if (CBaseCombatCharacter* character = target->MyCombatCharacterPointer()) {
				debug.relation = character->IRelationType(player);
			}
		}
		if (npc) {
			debug.npcState = npc->GetState();
			if (const CAI_Schedule* schedule = npc->GetCurSchedule()) {
				copy_utf8(debug.schedule, schedule->GetName());
			}
		}
		link.write_host_debug_server(debug);

		// one line when the crosshair moves onto a different npc, to check the screen against. sticky,
		// so sweeping across the same npc in a fight doesn't log it again; by handle, so a new npc in
		// a reused entity slot still counts as different.
		if (npc && npc->GetRefEHandle().ToInt() != logged_npc_) {
			logged_npc_ = npc->GetRefEHandle().ToInt();
			log_info("debug screen: crosshair on %s #%d (%d/%d health)", npc->GetClassname(), npc->entindex(), debug.health, debug.maxHealth);
		}
	}
}

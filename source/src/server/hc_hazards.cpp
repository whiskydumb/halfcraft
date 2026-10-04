// server.dll: minecraft's fire, lava and magma hurt half-life's npcs (see hc_hazards.h).

#include "cbase.h"
#include "ai_basenpc.h"
#include "player.h"
#include "takedamageinfo.h"
#include "world.h"

#include "tier0/valve_minmax_off.h"
#include <cmath>

#include "core/hc_link.h"
#include "core/hc_module.h"
#include "core/hc_units.h"
#include "server/hc_hazards.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr float CHECK_SECONDS = 0.25f;
		constexpr float RANGE = 64.0f * static_cast<float>(UNITS_PER_BLOCK);  // minecraft's blocks are only around its player
		constexpr float BURN_SECONDS = 6.0f;      // a mob in minecraft fire burns for 8 seconds
		// per check (a quarter second), in half-life points (minecraft's x5): lava hits for 4 and magma
		// for 1, each every half second
		constexpr float LAVA_DAMAGE = 10.0f;
		constexpr float MAGMA_DAMAGE = 2.5f;

		HazardAtFn hazard_lookup()
		{
			static HazardAtFn lookup = nullptr;
			if (!lookup) {
				lookup = reinterpret_cast<HazardAtFn>(find_export("client.dll", HC_HAZARD_AT_EXPORT));
			}
			return lookup;
		}

		void burn(CAI_BaseNPC* npc, float damage)
		{
			npc->Ignite(BURN_SECONDS);
			if (damage > 0.0f) {
				CTakeDamageInfo info(GetWorldEntity(), GetWorldEntity(), damage, DMG_BURN);
				npc->TakeDamage(info);
			}
		}
	}

	void Hazards::update(CBasePlayer* player, int slot)
	{
		if (!player || gpGlobals->curtime < next_check_) {
			return;
		}
		next_check_ = gpGlobals->curtime + CHECK_SECONDS;
		const HazardAtFn hazard_at = hazard_lookup();
		if (!hazard_at) {
			return;
		}
		CAI_BaseNPC** npcs = g_AI_Manager.AccessAIs();
		for (int i = 0; i < g_AI_Manager.NumAIs(); ++i) {
			CAI_BaseNPC* npc = npcs[i];
			if (!npc || !npc->IsAlive() || npc->m_takedamage == DAMAGE_NO || (npc->GetAbsOrigin() - player->GetAbsOrigin()).LengthSqr() > RANGE * RANGE) {
				continue;
			}
			// the block the feet are in, and the one they stand on
			const Vector& origin = npc->GetAbsOrigin();
			const float   feet[3] = { origin.x, origin.y, origin.z + 2.0f };
			const McVec   mc = source_to_mc(feet, slot);
			const int     x = static_cast<int>(std::floor(mc.x)), y = static_cast<int>(std::floor(mc.y)), z = static_cast<int>(std::floor(mc.z));
			const int     inside = hazard_at(x, y, z);
			if (inside == proto::kHazardLava) {
				burn(npc, LAVA_DAMAGE);
			} else if (inside == proto::kHazardFire) {
				burn(npc, 0.0f);  // the afterburn does the damage
			} else if (hazard_at(x, y - 1, z) == proto::kHazardMagma && npc->GetGroundEntity()) {
				CTakeDamageInfo info(GetWorldEntity(), GetWorldEntity(), MAGMA_DAMAGE, DMG_BURN);
				npc->TakeDamage(info);
			}
		}
	}
}

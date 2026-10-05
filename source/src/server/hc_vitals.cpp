// server.dll: minecraft's health mirrored onto half-life's player (see hc_vitals.h).

#include "cbase.h"
#include "player.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>

#include "core/hc_log.h"
#include "core/hc_module.h"
#include "server/hc_vitals.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr float SOURCE_PER_MINECRAFT = 5.0f;  // minecraft divides host damage and healing by 5
		constexpr int   MAX_ARMOR = 100;              // half-life's full suit
		constexpr float PENDING_SECONDS = 1.0f;       // healing minecraft hasn't shown by then was refused (full already)
		constexpr int   DROWN_DEBT_RESET = 1 << 30;   // CBasePlayer::AdjustDrownDmg clamps the count to what it gave back already

		bool g_minecraft_owns_health = false;

		void push_heal(int kind, float amount)
		{
			static PushHealFn push = nullptr;
			if (!push) {
				push = reinterpret_cast<PushHealFn>(find_export("client.dll", HC_PUSH_HEAL_EXPORT));
				if (!push) {
					return;
				}
			}
			push(kind, amount);
		}
	}

	int Vitals::Mirror::update(int current, float minecraft, int kind, float frametime)
	{
		// half-life added some since the last frame (a kit, a charger tick, a battery, a medic)
		if (set >= 0 && current > set) {
			const float gain = static_cast<float>(current - set);
			push_heal(kind, gain);
			pending += gain;
			waited = 0.0f;
		}
		// minecraft's own rise uses up what's pending
		if (seen >= 0.0f && minecraft > seen) {
			pending = std::max(0.0f, pending - (minecraft - seen) * SOURCE_PER_MINECRAFT);
		}
		seen = minecraft;
		if (pending > 0.0f && (waited += frametime) > PENDING_SECONDS) {
			pending = 0.0f;
		}
		return static_cast<int>(std::lround(minecraft * SOURCE_PER_MINECRAFT + pending));
	}

	bool minecraft_owns_health()
	{
		return g_minecraft_owns_health;
	}

	void Vitals::update(Link& link, CBasePlayer* player)
	{
		proto::McState mc{};
		g_minecraft_owns_health = player && player->IsAlive() && link.mc_alive() && link.read_mc_state(mc) && (mc.flags & proto::kMcInWorld) &&
								  !(mc.flags & proto::kMcDead) && mc.maxHealth > 0.0f;
		if (!g_minecraft_owns_health) {
			health_.reset();
			armor_.reset();
			return;
		}
		// minecraft drowns the player on its own air (server_player_damage drops half-life's DMG_DROWN), but
		// half-life still counts the drowning it would have dealt and gives it back as health once the
		// player surfaces: drop that count so its recovery never heals minecraft for damage it never took
		player->AdjustDrownDmg(-DROWN_DEBT_RESET);
		const float frametime = gpGlobals->frametime;
		const int   max_health = static_cast<int>(std::lround(mc.maxHealth * SOURCE_PER_MINECRAFT));
		const int   health = std::clamp(health_.update(player->GetHealth(), mc.health, proto::kHealHealth, frametime), 1, max_health);
		player->SetMaxHealth(max_health);
		player->SetHealth(health);
		health_.set = health;

		const int armor = std::clamp(armor_.update(player->ArmorValue(), mc.absorption, proto::kHealArmor, frametime), 0, MAX_ARMOR);
		player->SetArmorValue(armor);
		armor_.set = armor;
	}
}

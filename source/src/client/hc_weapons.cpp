// client.dll: half-life's weapons as minecraft items (see hc_weapons.h).

#include "cbase.h"
#include "c_baseplayer.h"
#include "cdll_client_int.h"
#include "iclientmode.h"
#include "in_buttons.h"
#include "inputsystem/iinputsystem.h"
#include "usercmd.h"
#include "view_shared.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "client/hc_client.h"
#include "client/hc_weapons.h"
#include "core/hc_log.h"
#include "shared/hc_hooks.h"
#include "shared/hc_weapon_ids.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr float DEGREES = 3.14159265f / 180.0f;
		constexpr float ZOOM_EPSILON = 0.5f;  // degrees under the default fov that count as zoomed

		bool          g_ready = false;        // the weapon minecraft holds is out (as of the last command)
		std::uint32_t g_said_held = ~0u;      // what minecraft held and source had out when last logged
		std::uint32_t g_said_out = ~0u;

		const char* name_of(std::uint32_t id)
		{
			const char* classname = weapon_classname(id);
			return classname ? classname : "nothing";
		}

		/// server.dll's weapon table, when it has a player. the weapon really out is its active one: the
		/// client's own active weapon is predicted, it takes every switch weaponselect asks for, even one
		/// the server refuses (the rpg while its rocket flies), and clicks would reach the weapon that's
		/// really out.
		bool live_weapons(const ClientSession& session, proto::WeaponTable& table)
		{
			return session.link.read_weapons(table) && (table.flags & proto::kWeaponTableLive);
		}

		/// the player's weapon minecraft knows by id, if it has it.
		C_BaseCombatWeapon* owned_weapon(C_BasePlayer* player, std::uint32_t id)
		{
			const char* classname = weapon_classname(id);
			if (!classname) {
				return nullptr;
			}
			for (int i = 0; i < player->WeaponCount(); ++i) {
				C_BaseCombatWeapon* weapon = player->GetWeapon(i);
				if (weapon && !Q_strcmp(weapon->GetName(), classname)) {
					return weapon;
				}
			}
			return nullptr;
		}

		struct Release
		{
			ButtonCode_t code;
			double       at;  // Plat_FloatTime()
		};

		// hc_press presses that let go by themselves
		std::vector<Release> g_releases;

		void press(ButtonCode_t code, bool down)
		{
			if (client_key_event(down ? 1 : 0, code, engine->Key_BindingForKey(code))) {
				log_info("hc_press %s: source keeps it (minecraft doesn't have the input)", inputsystem->ButtonCodeToString(code));
			}
		}

		class WeaponReleases final : public CAutoGameSystemPerFrame
		{
		public:
			WeaponReleases() : CAutoGameSystemPerFrame("HalfCraftWeaponReleases") {}

			void Update(float) override
			{
				const double now = Plat_FloatTime();
				for (auto it = g_releases.begin(); it != g_releases.end();) {
					if (it->at <= now) {
						const ButtonCode_t code = it->code;
						it = g_releases.erase(it);
						press(code, false);
					} else {
						++it;
					}
				}
			}
		};

		WeaponReleases g_weapon_releases;
	}

	bool weapon_in_hand(const ClientSession& session)
	{
		return session.have_mc && session.mc_in_world && session.mc.heldWeapon != proto::kHostWeaponNone;
	}

	void weapon_create_move(ClientSession& session, CUserCmd* cmd)
	{
		g_ready = false;
		C_BasePlayer* player = C_BasePlayer::GetLocalPlayer();
		if (!player || !session.have_mc || !session.mc_in_world) {
			return;
		}
		proto::WeaponTable  table{};
		const bool          live = live_weapons(session, table);
		const std::uint32_t held = session.mc.heldWeapon;
		const std::uint32_t out = live ? table.active : proto::kHostWeaponNone;
		// a player_speedmod keeps the weapons away: the hand asks for none
		const bool suppressed = live && (table.flags & proto::kWeaponTableSuppressed);
		const bool          matched = held != proto::kHostWeaponNone && held == out;
		g_ready = matched && !session.mc_screen_open;

		// half-life's viewmodel stands in for minecraft's hand, so only in minecraft's first person
		if (matched && session.minecraft_hud && session.mc.cameraMode == 0 && !player->IsInAVehicle()) {
			cmd->hc_flags |= HC_CMD_VIEWMODEL;
		}
		// asked every command: source can refuse or delay a switch (the rpg's rocket in flight, the
		// gravity gun holding something, a weapon still coming out). while use carries a prop source
		// has put the weapon away itself and takes it out again after. on a ladder or a ride minecraft's
		// hotbar still picks it (hc_input.cpp)
		if ((session.minecraft_owns_player || session.minecraft_hands) && !session.holding) {
			C_BaseCombatWeapon* wanted = held != proto::kHostWeaponNone && !suppressed ? owned_weapon(player, held) : nullptr;
			cmd->hc_flags |= HC_CMD_WEAPONS;
			cmd->weaponselect = wanted ? wanted->entindex() : 0;
			cmd->weaponsubtype = wanted ? wanted->GetSubType() : 0;
		}

		if (held != g_said_held || out != g_said_out) {
			g_said_held = held;
			g_said_out = out;
			log_info("weapons: minecraft holds %s, source has %s out", name_of(held), name_of(out));
		}
	}

	int weapon_buttons(const ClientSession& session)
	{
		int buttons = 0;
		if (session.holding || g_ready) {
			buttons |= (session.attack_held ? IN_ATTACK : 0) | (session.attack2_held ? IN_ATTACK2 : 0);
		}
		if (g_ready && session.reload_held) {
			buttons |= IN_RELOAD;
		}
		return buttons;
	}

	float weapon_zoom_fov(float fov)
	{
		C_BasePlayer* player = C_BasePlayer::GetLocalPlayer();
		if (!player) {
			return fov;
		}
		const float base = static_cast<float>(player->GetDefaultFOV());
		const float zoomed = player->GetFOV();
		if (zoomed <= 0.0f || zoomed >= base - ZOOM_EPSILON) {
			return fov;
		}
		// source narrows its own view from base to zoomed: the same narrowing, in tangents
		const float scale = std::tan(zoomed * 0.5f * DEGREES) / std::tan(base * 0.5f * DEGREES);
		return 2.0f * std::atan(std::tan(fov * 0.5f * DEGREES) * scale) / DEGREES;
	}

	void client_viewmodel_view(CViewSetup& view, Vector& origin, QAngle& angles)
	{
		auto& s = client_session();
		if (!s.puppeting || !s.pose_valid) {
			return;
		}
		// the camera is minecraft's (its eye, bob and roll); the gun stays put in front of it
		origin = view.origin;
		angles = view.angles;
		// minecraft's fov widens when sprinting and narrows in water: the gun keeps its size, and
		// only zooms with half-life's own zoom
		C_BasePlayer* player = C_BasePlayer::GetLocalPlayer();
		const float   zoom = player ? std::max(0.0f, static_cast<float>(player->GetDefaultFOV()) - player->GetFOV()) : 0.0f;
		view.fovViewmodel = g_pClientMode->GetViewModelFOV() - zoom;
	}
}

// a test helper: what the real keyboard and mouse do, through halfcraft's routing (hc_input.cpp). a
// half-life weapon minecraft holds fires, where hc_hold's clicks go straight to minecraft.
CON_COMMAND(hc_press, "halfcraft: press a key or mouse button as the real one would: hc_press <key, e.g. MOUSE1 MOUSE2 R G> <1 down | 0 up> [seconds before it lets go]")
{
	const ButtonCode_t code = args.ArgC() > 2 ? inputsystem->StringToButtonCode(args[1]) : BUTTON_CODE_INVALID;
	if (code == BUTTON_CODE_INVALID || code == BUTTON_CODE_NONE) {
		Msg("usage: hc_press <key> <1 | 0> [seconds], e.g. hc_press MOUSE1 1 0.2 (one shot)\n");
		return;
	}
	const bool   down = atoi(args[2]) != 0;
	const double seconds = down && args.ArgC() > 3 ? std::max(0.0, atof(args[3])) : 0.0;
	auto&        releases = halfcraft::g_releases;
	releases.erase(std::remove_if(releases.begin(), releases.end(), [code](const halfcraft::Release& r) { return r.code == code; }), releases.end());
	if (seconds > 0.0) {
		releases.push_back({ code, Plat_FloatTime() + seconds });
		halfcraft::log_info("hc_press %s: down for %.2f s", args[1], seconds);
	} else {
		halfcraft::log_info("hc_press %s: %s", args[1], down ? "down" : "up");
	}
	halfcraft::press(code, down);
}

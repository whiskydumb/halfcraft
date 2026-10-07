// client.dll: minecraft's own jumps (see hc_jumps.h).

#include "cbase.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "client/hc_client.h"
#include "client/hc_jumps.h"
#include "core/hc_log.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		// a teleport lands this long after minecraft counts it, at the latest (the feet go straight to it
		// with the tick that counts it, hc_ticks.cpp: normally the next frame)
		constexpr double LANDING_SECONDS = 1.0;
		// the commands after the landing stay flagged this long: a jump the client's prediction took and
		// the server refused shows up a frame or two later
		constexpr double SETTLE_SECONDS = 0.25;
		// the feet have landed once they are this close to minecraft's latest tick, on top of that
		// tick's own motion
		constexpr float LANDED_UNITS = 4.0f;
		// a step this long between two commands is one source's guard (160) may refuse
		constexpr float FAST_STEP_UNITS = 120.0f;
		// a frame covers up to about two of minecraft's ticks at a low frame rate
		constexpr float FAST_TICKS = 2.0f;
		constexpr double FAST_LOG_SECONDS = 1.0;

		bool          g_counted = false;
		std::uint32_t g_teleports = 0;      // minecraft's teleport count, as last seen
		double        g_flag_until = -1.0;  // Plat_FloatTime() until which commands are flagged (-1: none)
		bool          g_landed = false;
		bool          g_have_from = false;
		float         g_from[3] = {};  // the last puppet command's feet
		bool          g_sent = false;
		double        g_fast_logged = -1.0;
		// where source landed a jump that didn't fit where minecraft put its player (or kept the player,
		// refusing it), and until when minecraft's player is still to go there (-1: none)
		float         g_elsewhere[3] = {};
		bool          g_elsewhere_refused = false;
		double        g_elsewhere_until = -1.0;
		// the landing minecraft's player went to last: prediction runs the commands before it again
		// for a while, and they'd land there again
		float         g_went[3] = {};
		double        g_went_until = -1.0;

		float distance(const float a[3], const float b[3])
		{
			const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		/// how far minecraft's player moved over its latest tick (source units).
		float tick_motion(const ClientSession& s)
		{
			const double dx = s.mc.curX - s.mc.prevX, dy = s.mc.curY - s.mc.prevY, dz = s.mc.curZ - s.mc.prevZ;
			return static_cast<float>(std::sqrt(dx * dx + dy * dy + dz * dz) * UNITS_PER_BLOCK);
		}
	}

	int jump_flags(const ClientSession& s, const float feet[3])
	{
		const double now = Plat_FloatTime();
		if (!g_counted || s.mc.teleportCount != g_teleports) {
			if (g_counted) {
				log_info("minecraft moved its player by itself (teleport %u): source takes it where the player fits", s.mc.teleportCount);
				g_flag_until = now + LANDING_SECONDS;
				g_landed = false;
			}
			g_teleports = s.mc.teleportCount;
			g_counted = true;
		}
		const float step = g_have_from ? distance(feet, g_from) : 0.0f;
		std::copy(feet, feet + 3, g_from);
		g_have_from = true;

		bool jump = false;
		if (g_flag_until >= 0.0 && now < g_flag_until) {
			if (!g_landed) {
				// minecraft's latest tick already has the player where the teleport put it, and the feet
				// get there with it
				float latest[3];
				mc_to_source(s.mc.curX, s.mc.curY, s.mc.curZ, s.slot, latest);
				if (distance(feet, latest) <= tick_motion(s) + LANDED_UNITS) {
					g_landed = true;
					g_flag_until = std::min(g_flag_until, now + SETTLE_SECONDS);
				}
			}
			jump = true;
		} else {
			g_flag_until = -1.0;
		}

		if (!jump && step > FAST_STEP_UNITS && step <= tick_motion(s) * FAST_TICKS) {
			jump = true;
			if (g_fast_logged < 0.0 || now - g_fast_logged >= FAST_LOG_SECONDS) {
				g_fast_logged = now;
				log_info("minecraft's player moved %.0f units in one frame (%.0f a tick): source takes it where the player fits", step, tick_motion(s));
			}
		}
		g_sent = jump;
		return jump ? HC_CMD_JUMP : 0;
	}

	void jump_forget(const ClientSession& s)
	{
		g_counted = s.have_mc;
		g_teleports = s.mc.teleportCount;
		g_flag_until = -1.0;
		g_have_from = false;
		g_sent = false;
	}

	bool jump_in_flight()
	{
		return g_sent;
	}

	bool jump_landing_elsewhere()
	{
		if (g_elsewhere_until >= 0.0 && Plat_FloatTime() > g_elsewhere_until) {
			g_elsewhere_until = -1.0;
		}
		return g_elsewhere_until >= 0.0;
	}

	bool jump_landed_elsewhere(const float origin[3], bool& refused)
	{
		if (!jump_landing_elsewhere() || distance(origin, g_elsewhere) > LANDED_UNITS) {
			return false;  // source's player isn't there yet (it's drawn behind its commands)
		}
		refused = g_elsewhere_refused;
		g_elsewhere_until = -1.0;
		std::copy(g_elsewhere, g_elsewhere + 3, g_went);
		g_went_until = Plat_FloatTime() + LANDING_SECONDS;
		return true;
	}

	void client_jump_landed_elsewhere(const Vector& feet, bool refused)
	{
		// prediction runs a command again and again: the latest landing counts
		const float  at[3] = { feet.x, feet.y, feet.z };
		const double now = Plat_FloatTime();
		if (now < g_went_until && distance(at, g_went) <= LANDED_UNITS) {
			return;
		}
		std::copy(at, at + 3, g_elsewhere);
		g_elsewhere_refused = refused;
		g_elsewhere_until = now + LANDING_SECONDS;
	}
}

// server.dll: it landed one of minecraft's jumps next to where minecraft put its player, or refused it
// (hc_bridge.h)
extern "C" __declspec(dllexport) void HalfCraft_JumpLanded(const float feet[3], int refused)
{
	halfcraft::client_jump_landed_elsewhere(Vector(feet[0], feet[1], feet[2]), refused != 0);
}

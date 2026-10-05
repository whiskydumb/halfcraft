// client.dll: console commands that drive minecraft. hc_key and hc_hold press its keys and mouse
// buttons the way hc_input's hooks do, and hc_use holds source's own use (G); they're for test
// scripts (tools/hl2_command.ps1), since typed into the console they're let go again as it closes
// (an open console hands input back to source, and minecraft lets go of everything). hc_mc runs a
// minecraft command as the player (string channel kStrCommand; minecraft's HostCommand logs what it
// answers), from scripts and players alike.

#include "cbase.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "client/hc_client.h"
#include "core/hc_log.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		// how long a tap (hc_key without a state) stays down: minecraft reads movement keys (jump,
		// sneak) once per 50 ms tick, and a press released in the same frame never reaches one
		constexpr double TAP_SECONDS = 0.12;
		constexpr int    MAX_SCANCODE = 511;  // minecraft's InputBridge keeps 512 keys
		constexpr int    MAX_BUTTON = 5;

		struct Release
		{
			proto::InputType type;
			std::uint16_t    code;
			double           at;  // Plat_FloatTime()
		};

		// presses that let go by themselves
		std::vector<Release> g_releases;
		double               g_use_release = 0.0;  // when hc_use lets go of use; 0: it doesn't

		bool minecraft_linked(const char* command)
		{
			auto& s = client_session();
			if (s.link_ready && s.link.mc_alive()) {
				return true;
			}
			log_warning("%s: minecraft isn't linked; nothing sent", command);
			return false;
		}

		/// presses (down) or lets go of a minecraft key (kInKey) or mouse button (kInMouseButton);
		/// a press with seconds > 0 lets go by itself after that long
		void set_held(proto::InputType type, std::uint16_t code, bool down, double seconds)
		{
			g_releases.erase(std::remove_if(g_releases.begin(), g_releases.end(), [&](const Release& r) { return r.type == type && r.code == code; }),
				g_releases.end());
			client_session().link.push_input(type, code, down ? 1 : 0);
			if (down && seconds > 0.0) {
				g_releases.push_back({ type, code, Plat_FloatTime() + seconds });
			}
		}

		/// hc_key's and hc_hold's arguments after the code: [state] [seconds]
		void press_from_args(const CCommand& args, proto::InputType type, std::uint16_t code, const char* command)
		{
			if (args.ArgC() < 3) {
				set_held(type, code, true, TAP_SECONDS);
				log_info("%s %d: tapped", command, code);
				return;
			}
			const bool   down = atoi(args[2]) != 0;
			const double seconds = args.ArgC() > 3 ? std::max(0.0, atof(args[3])) : 0.0;
			set_held(type, code, down, seconds);
			if (!down) {
				log_info("%s %d: up", command, code);
			} else if (seconds > 0.0) {
				log_info("%s %d: down for %.2f s", command, code, seconds);
			} else {
				log_info("%s %d: down", command, code);
			}
		}

		class ConsoleInputSystem final : public CAutoGameSystemPerFrame
		{
		public:
			ConsoleInputSystem() : CAutoGameSystemPerFrame("HalfCraftConsoleInput") {}

			void Update(float) override
			{
				const double now = Plat_FloatTime();
				if (g_use_release > 0.0 && g_use_release <= now) {
					client_session().use_held = false;
					g_use_release = 0.0;
				}
				if (g_releases.empty()) {
					return;
				}
				auto&        link = client_session().link;
				for (const auto& release : g_releases) {
					if (release.at <= now) {
						link.push_input(release.type, release.code, 0);
					}
				}
				g_releases.erase(std::remove_if(g_releases.begin(), g_releases.end(), [now](const Release& r) { return r.at <= now; }), g_releases.end());
			}
		};

		ConsoleInputSystem g_console_input;
	}
}

CON_COMMAND(hc_key, "halfcraft: press a minecraft key: hc_key <sdl scancode> [1 down | 0 up] [seconds before it lets go]; without a state it's tapped")
{
	const int scancode = args.ArgC() > 1 ? atoi(args[1]) : 0;
	if (scancode < 1 || scancode > halfcraft::MAX_SCANCODE) {
		Msg("usage: hc_key <sdl scancode 1-%d> [1 | 0] [seconds], e.g. hc_key 44 (space)\n", halfcraft::MAX_SCANCODE);
		return;
	}
	if (halfcraft::minecraft_linked("hc_key")) {
		halfcraft::press_from_args(args, halfcraft::proto::kInKey, static_cast<std::uint16_t>(scancode), "hc_key");
	}
}

CON_COMMAND(hc_hold, "halfcraft: hold a minecraft mouse button: hc_hold <1 left | 2 middle | 3 right | 4 | 5> <1 down | 0 up> [seconds before it lets go]")
{
	const int button = args.ArgC() > 2 ? atoi(args[1]) : 0;
	if (button < 1 || button > halfcraft::MAX_BUTTON) {
		Msg("usage: hc_hold <mouse button 1-%d> <1 | 0> [seconds], e.g. hc_hold 3 1 1.5 (draws a bow for 1.5 s)\n", halfcraft::MAX_BUTTON);
		return;
	}
	if (halfcraft::minecraft_linked("hc_hold")) {
		halfcraft::press_from_args(args, halfcraft::proto::kInMouseButton, static_cast<std::uint16_t>(button), "hc_hold");
	}
}

CON_COMMAND(hc_use, "halfcraft: hold source's use (G: doors, buttons, chargers): hc_use <1 down | 0 up> [seconds before it lets go]")
{
	if (args.ArgC() < 2) {
		Msg("usage: hc_use <1 | 0> [seconds], e.g. hc_use 1 5 (a charger for 5 s)\n");
		return;
	}
	const bool   down = atoi(args[1]) != 0;
	const double seconds = down && args.ArgC() > 2 ? std::max(0.0, atof(args[2])) : 0.0;
	halfcraft::client_session().use_held = down;
	halfcraft::g_use_release = seconds > 0.0 ? Plat_FloatTime() + seconds : 0.0;
	if (seconds > 0.0) {
		halfcraft::log_info("hc_use: down for %.2f s", seconds);
	} else {
		halfcraft::log_info("hc_use: %s", down ? "down" : "up");
	}
}

CON_COMMAND(hc_mc, "halfcraft: run a minecraft command as the player: hc_mc <command> (quote it when it has a ; or //)")
{
	// the rest of the line as typed: minecraft's own syntax ({}, :, quotes) would be split up by
	// source's tokenizer
	std::string command = args.ArgS();
	const auto  first = command.find_first_not_of(" \t");
	const auto  last = command.find_last_not_of(" \t\r\n");
	command = first == std::string::npos ? std::string() : command.substr(first, last - first + 1);
	// quoted whole (for a ; or //): source's tokenizer already took the quotes off its one argument;
	// quotes inside the command (tellraw's json) stay
	if (args.ArgC() == 2 && !command.empty() && command.front() == '"') {
		command = args[1];
	}
	if (command.empty()) {
		Msg("usage: hc_mc <minecraft command>, e.g. hc_mc give @s ender_pearl 16\n");
		return;
	}
	if (!halfcraft::minecraft_linked("hc_mc")) {
		return;
	}
	if (halfcraft::client_session().link.push_string(halfcraft::proto::kStrCommand, command)) {
		halfcraft::log_info("hc_mc: sent %s", command.c_str());
	} else {
		halfcraft::log_warning("hc_mc: minecraft's input ring is full; %s dropped", command.c_str());
	}
}

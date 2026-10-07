#pragma once

// client.dll side of halfcraft: what the per-frame update, the input hooks, the camera and the
// overlay share. include after the sdk headers and tier0/valve_minmax_off.h.

#include <cstdint>
#include <mutex>
#include <vector>

#include "core/hc_link.h"
#include "core/hc_ticks.h"
#include "core/hc_units.h"

namespace halfcraft
{
	struct ClientSession
	{
		Link link;
		bool link_ready = false;

		// minecraft, as of this frame
		proto::McState mc{};
		bool           have_mc = false;       // connected and its state read
		bool           mc_was_alive = false;
		std::uint32_t  last_mc_pid = 0;
		bool           mc_in_world = false;
		bool           mc_screen_open = false;  // an inventory, chat, ... is open
		float          sensitivity = 0.5f;     // minecraft's mouse sensitivity option

		// source, as of this frame
		bool source_menu_open = false;  // pause menu, console, loading: source owns input
		bool loading = true;
		int  viewport_w = 1920;
		int  viewport_h = 1080;

		// who drives the player
		bool puppeting = false;              // minecraft's position drives source's player
		bool minecraft_owns_player = false;  // puppeting, or waiting for minecraft after a teleport
		bool minecraft_owns_input = false;   // keys and mouse go to minecraft
		bool minecraft_hands = false;        // on a ladder or a ride: source moves the player, minecraft keeps its hands (hc_input.cpp)
		bool minecraft_camera = false;       // on a ladder, a ride or in a vehicle: minecraft's F5 camera goes round source's view
		bool seated = false;                 // in a vehicle's seat: minecraft's body sits (HostState's seat)
		bool minecraft_hud = false;          // minecraft's overlay (hud, hand, screens) is shown

		// look (minecraft degrees), integrated from the raw mouse so the camera has no extra latency
		float yaw = 0.0f;
		float pitch = 0.0f;
		bool  look_initialized = false;
		int   cursor_x = 0;  // minecraft's cursor while one of its screens is open (overlay pixels)
		int   cursor_y = 0;

		// teleport handshake: minecraft moves its player to the host position when the seq changes
		std::uint32_t teleport_seq = 1;
		bool          teleport_pending = true;
		float         hold_mismatch = 0.0f;

		// the map
		std::uint32_t world_id = 0;
		MapSlot       slot;
		std::uint32_t epoch = 0;

		// where minecraft put the player last (source units), to notice source moving them itself
		float last_set[3] = {};
		bool  have_last_set = false;

		TickInterpolator ticks;
		McPose           pose{};
		bool             pose_valid = false;

		// minecraft's F5 camera as the view uses it: how far it sits from the eye (blocks), eased
		float         camera_zoom = 0.0f;
		std::uint32_t camera_zoom_mode = 0;
		// while source moves the player and minecraft's camera goes round source's view: that view's
		// eye this frame (source units), which minecraft's body goes under in third person
		float takeover_eye[3] = {};
		bool  have_takeover_eye = false;

		// source's use while minecraft has the keyboard (G, or whatever source binds +use to), and
		// minecraft's forward key (W: walking into a ladder mounts it)
		bool use_held = false;
		bool forward_held = false;

		// carrying a prop with use (server.dll says, HalfCraft_SetHolding): the mouse buttons throw
		// (left) and drop (right) it the way half-life's do, instead of going to minecraft
		bool holding = false;
		bool attack_held = false;
		bool attack2_held = false;
		bool reload_held = false;  // R while minecraft holds a half-life weapon (hc_weapons.cpp)

		// what server.dll has for the input ring this client owns: half-life's hits and heals on the
		// player (hc_bridge.h)
		std::mutex                     server_inputs_lock;
		std::vector<proto::InputEvent> server_inputs;
	};

	ClientSession& client_session();

	/// key/mouse bookkeeping when input moves between the games (hc_input.cpp).
	void input_release_all(ClientSession& session);
}

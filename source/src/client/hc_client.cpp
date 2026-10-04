// client.dll: the per-frame link to minecraft. who drives the player, the teleport handshake,
// the host state minecraft reads, and the camera. ported from SkyCraft's Game.cpp.

#include "cbase.h"
#include "c_baseplayer.h"
#include "iinput.h"
#include "ienginevgui.h"
#include "in_buttons.h"
#include "usercmd.h"
#include "view_shared.h"
#include "ivrenderview.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>

#include "client/hc_block_lights.h"
#include "client/hc_blocks.h"
#include "client/hc_client.h"
#include "client/hc_things.h"
#include "core/hc_log.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr float PI = 3.14159265f;
		constexpr float TELEPORT_THRESHOLD = 160.0f;  // units; bigger jumps are source moving the player
		constexpr float HOLD_MISMATCH_BLOCKS = 8.0f;   // minecraft waiting this far away is waiting in the wrong place
		constexpr float IDLE_GAME_HOUR = 12.0f;        // half-life has no day: minecraft keeps noon
		constexpr std::uint64_t RENDER_DRAIN_BYTES = 32ull << 20;  // per frame; the atlas alone is ~20 MB
		constexpr float CAMERA_EASE_SECONDS = 0.2f;  // the F5 camera easing back out after something pushed it in

		void console_sink(int severity, const char* line)
		{
			if (severity == LOG_INFO) {
				Msg("%s\n", line);
			} else {
				Warning("%s\n", line);
			}
		}

		/// whether the player stands on a lift, train or other pusher that moved lately. source carries
		/// riders along; minecraft can't. (pushers' velocities aren't networked: watch them move.)
		class PlatformWatch
		{
		public:
			bool riding(C_BasePlayer* player)
			{
				// what's under the feet (not the predicted ground entity: while minecraft still drives
				// the player, a rising lift has the feet inside its floor for a moment)
				trace_t      trace;
				const Vector feet = player->GetAbsOrigin();
				UTIL_TraceHull(feet + Vector(0.0f, 0.0f, 24.0f), feet - Vector(0.0f, 0.0f, 12.0f), Vector(-12.0f, -12.0f, 0.0f), Vector(12.0f, 12.0f, 4.0f),
					MASK_PLAYERSOLID, player, COLLISION_GROUP_PLAYER_MOVEMENT, &trace);
				C_BaseEntity* ground = trace.m_pEnt;
				if (!ground || ground->GetMoveType() != MOVETYPE_PUSH) {
					ground_ = nullptr;
					return false;
				}
				const float now = gpGlobals->curtime;
				if (ground_.Get() != ground) {
					ground_ = ground;
					moved_at_ = -1.0f;
				} else if ((ground->GetAbsOrigin() - origin_).LengthSqr() > MOVE_EPSILON * MOVE_EPSILON ||
						   std::fabs(AngleDiff(ground->GetAbsAngles().y, angles_.y)) > MOVE_EPSILON ||
						   std::fabs(AngleDiff(ground->GetAbsAngles().x, angles_.x)) > MOVE_EPSILON ||
						   std::fabs(AngleDiff(ground->GetAbsAngles().z, angles_.z)) > MOVE_EPSILON) {
					moved_at_ = now;
				}
				origin_ = ground->GetAbsOrigin();
				angles_ = ground->GetAbsAngles();
				return moved_at_ >= 0.0f && now - moved_at_ < SETTLE_SECONDS;
			}

		private:
			static constexpr float MOVE_EPSILON = 0.05f;   // units or degrees a frame
			static constexpr float SETTLE_SECONDS = 0.5f;  // standing still this long: the ride is over

			EHANDLE ground_;
			Vector  origin_;
			QAngle  angles_;
			float   moved_at_ = -1.0f;
		};

		// the takeovers that aren't the player playing (minecraft's hud would be in the way)
		constexpr char TAKEOVER_OBSERVING[] = "observing";
		constexpr char TAKEOVER_CAMERA[] = "scripted camera";

		/// why source keeps the player to itself right now (nullptr: it doesn't).
		const char* source_takeover(C_BasePlayer* player, bool riding)
		{
			if (player->GetVehicle()) {
				return "in a vehicle";
			}
			if (player->GetObserverMode() != OBS_MODE_NONE) {
				return TAKEOVER_OBSERVING;
			}
			if (player->GetMoveType() != MOVETYPE_WALK) {
				return "ladder or noclip";
			}
			if (player->GetFlags() & (FL_FROZEN | FL_ATCONTROLS)) {
				return "frozen by the map";
			}
			if (render->GetViewEntity() != player->entindex()) {
				return TAKEOVER_CAMERA;
			}
			if (riding) {
				return "riding a lift or train";
			}
			return nullptr;
		}

		float distance(const float a[3], const float b[3])
		{
			const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
			return std::sqrt(dx * dx + dy * dy + dz * dz);
		}

		class HalfCraftClientSystem final : public CAutoGameSystemPerFrame
		{
		public:
			HalfCraftClientSystem() : CAutoGameSystemPerFrame("HalfCraftClient") {}

			bool Init() override
			{
				set_log_sink(console_sink);
				auto& s = client_session();
				// starts somewhere new each run, so a minecraft still acknowledging the last run's
				// teleport can't be taken for having arrived at this run's.
				s.teleport_seq = static_cast<std::uint32_t>(qpc_now()) | 1u;
				s.link_ready = s.link.create();
				return true;
			}

			void Shutdown() override
			{
				blocks_shutdown();
				auto& s = client_session();
				if (s.link_ready) {
					s.link.write_host_state(proto::HostState{});
				}
				set_log_sink(nullptr);
			}

			void Update(float frametime) override { update(frametime); }

			// every level load: a map change, a transition, a save loaded (also of the same map)
			void LevelInitPostEntity() override { level_loaded_ = true; }

		private:
			void update(float frametime);

			const char*   takeover_ = nullptr;
			PlatformWatch platform_;
			bool          level_loaded_ = false;
		};

		HalfCraftClientSystem g_client_system;

		void HalfCraftClientSystem::update(float frametime)
		{
			auto& s = client_session();
			if (!s.link_ready) {
				return;
			}
			s.link.heartbeat();
			engine->GetScreenSize(s.viewport_w, s.viewport_h);

			// ---- minecraft -------------------------------------------------------------------
			const bool mc_alive = s.link.mc_alive();
			s.have_mc = mc_alive && s.link.read_mc_state(s.mc);
			const auto mc_pid = s.link.mc_pid();
			const bool new_mc_process = mc_pid != 0 && mc_pid != s.last_mc_pid;
			if (new_mc_process) {
				s.last_mc_pid = mc_pid;
			}
			if (mc_alive && (!s.mc_was_alive || new_mc_process)) {
				// minecraft (re)connected: everything goes to it again from a fresh epoch.
				log_info("minecraft connected (pid %u)", mc_pid);
				s.link.reset_overlay();
				++s.epoch;
				s.teleport_pending = true;
				s.ticks.reset();
			} else if (!mc_alive && s.mc_was_alive) {
				log_info("minecraft disconnected");
			}
			s.mc_was_alive = mc_alive;
			s.mc_in_world = s.have_mc && (s.mc.flags & proto::kMcInWorld);
			const bool screen_open = s.have_mc && (s.mc.flags & proto::kMcScreenOpen);
			if (screen_open && !s.mc_screen_open) {
				s.cursor_x = s.viewport_w / 2;
				s.cursor_y = s.viewport_h / 2;
			}
			s.mc_screen_open = screen_open;
			if (s.have_mc && s.mc.sensitivity > 0.0f) {
				s.sensitivity = s.mc.sensitivity;
			}

			// ---- source ----------------------------------------------------------------------
			C_BasePlayer* player = C_BasePlayer::GetLocalPlayer();
			const bool    in_game = player && engine->IsInGame() && !engine->IsLevelMainMenuBackground();
			s.loading = !in_game || engine->IsDrawingLoadingImage();
			const bool menu = enginevgui->IsGameUIVisible() || engine->Con_IsVisible() || engine->IsPaused();
			s.source_menu_open = menu || s.loading;

			// the map: a new one gets its own slot. every level load wipes minecraft's collision (a save
			// of this same map can have its doors and lifts somewhere else) and puts minecraft's player
			// where source's is now.
			if (in_game && level_loaded_) {
				level_loaded_ = false;
				const char* level = engine->GetLevelName();
				const auto  id = map_world_id(level);
				if (id != s.world_id) {
					s.world_id = id;
					s.slot = map_slot(level);
					log_info("map %s (slot %d)", level, s.slot);
				} else {
					log_info("map %s loaded again (a save)", level);
				}
				++s.epoch;
				s.teleport_pending = true;
				s.ticks.reset();
			}
			blocks_set_slot(s.slot);

			// half-life's hits and heals on the player go to minecraft's health
			{
				std::lock_guard<std::mutex> lock(s.server_inputs_lock);
				for (const auto& input : s.server_inputs) {
					s.link.push_input(static_cast<proto::InputType>(input.type), input.code, input.a, input.b, input.c);
				}
				s.server_inputs.clear();
			}

			// minecraft's meshes, atlas and the rest of what it draws through us
			s.link.drain_render(
				[](std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes) {
					blocks_on_message(type, payload, bytes);
					things_on_message(type, payload, bytes);
					block_lights_on_message(type, payload, bytes);
				},
				RENDER_DRAIN_BYTES);
			blocks_update();

			float origin[3] = { 0.0f, 0.0f, 0.0f };
			if (player) {
				const Vector& abs = player->GetAbsOrigin();
				origin[0] = abs.x, origin[1] = abs.y, origin[2] = abs.z;
			}

			// source moved the player itself (a teleport trigger, a level transition, loading a save).
			if (s.loading) {
				s.teleport_pending = true;
				s.have_last_set = false;
			} else if (s.puppeting && s.have_last_set && distance(origin, s.last_set) > TELEPORT_THRESHOLD) {
				log_info("source moved the player (%.0f units); resyncing minecraft", distance(origin, s.last_set));
				s.teleport_pending = true;
				s.have_last_set = false;
			}

			const char* takeover = in_game ? source_takeover(player, platform_.riding(player)) : nullptr;
			if (takeover != takeover_) {
				if (takeover) {
					log_info("source takes the player (%s; flags %#x, movetype %d)", takeover, player->GetFlags(), static_cast<int>(player->GetMoveType()));
				} else {
					log_info("source hands the player back");
					s.teleport_pending = true;  // minecraft picks up wherever source left the player
				}
			}
			takeover_ = takeover;

			if (s.teleport_pending && !s.loading) {
				++s.teleport_seq;
				s.teleport_pending = false;
				QAngle angles;
				engine->GetViewAngles(angles);
				s.yaw = source_yaw_to_mc(angles.y);
				s.pitch = angles.x;
				s.look_initialized = true;
			}
			if (!s.look_initialized) {
				QAngle angles;
				engine->GetViewAngles(angles);
				s.yaw = source_yaw_to_mc(angles.y);
				s.pitch = angles.x;
				s.look_initialized = true;
			}

			// minecraft holds its player still after a teleport until the ground has arrived around
			// them. if it's holding somewhere source's player isn't, that ground never comes: send it
			// again to where the player really is.
			const bool alive = player && player->IsAlive();
			const bool arriving = s.have_mc && s.mc_in_world && !s.loading && s.mc.teleportAck != s.teleport_seq && !takeover;
			if (arriving) {
				const auto   here = source_to_mc(origin, s.slot);
				const double gap = std::sqrt((here.x - s.mc.x) * (here.x - s.mc.x) + (here.y - s.mc.y) * (here.y - s.mc.y) + (here.z - s.mc.z) * (here.z - s.mc.z));
				s.hold_mismatch = gap > HOLD_MISMATCH_BLOCKS ? s.hold_mismatch + frametime : 0.0f;
				if (s.hold_mismatch > 1.0f) {
					log_info("minecraft is waiting %.0f blocks from source's player; teleporting it again", gap);
					s.teleport_pending = true;
					s.hold_mismatch = 0.0f;
				}
			} else {
				s.hold_mismatch = 0.0f;
			}

			const bool puppet = s.have_mc && s.mc_in_world && s.mc.teleportAck == s.teleport_seq && !s.loading && alive && !takeover;
			if (puppet != s.puppeting) {
				log_info("puppet %s", puppet ? "on (minecraft drives the player)" : "off");
			}
			s.puppeting = puppet;
			s.minecraft_owns_player = puppet || (arriving && alive);
			// on ladders, rides and in vehicles it's still minecraft's player: its hearts and hotbar stay up
			s.minecraft_hud = s.have_mc && s.mc_in_world && !s.loading && alive && takeover != TAKEOVER_OBSERVING && takeover != TAKEOVER_CAMERA;

			const bool owns_input = s.have_mc && s.mc_in_world && !s.source_menu_open && alive && !takeover;
			if (owns_input != s.minecraft_owns_input) {
				input_release_all(s);
				if (owns_input) {
					::input->ClearStates();  // nothing source thought was held stays held
				}
			}
			s.minecraft_owns_input = owns_input;

			if (s.have_mc) {
				s.pose = s.ticks.sample(s.mc, qpc_now());
				s.pose_valid = true;
			} else {
				s.pose_valid = false;
			}

			// tell minecraft where source's player is and where they look.
			proto::HostState host{};
			host.flags = (in_game ? proto::kHostInGame : 0u) | (menu ? proto::kHostMenuOpen : 0u) | (s.loading ? proto::kHostLoading : 0u);
			const auto mc_pos = source_to_mc(origin, s.slot);
			host.worldId = s.world_id;
			host.collisionEpoch = s.epoch;
			host.posX = mc_pos.x;
			host.posY = mc_pos.y;
			host.posZ = mc_pos.z;
			host.yaw = s.yaw;
			host.pitch = s.pitch;
			host.teleportSeq = s.teleport_seq;
			host.viewportW = static_cast<std::uint32_t>(s.viewport_w);
			host.viewportH = static_cast<std::uint32_t>(s.viewport_h);
			host.gameHour = IDLE_GAME_HOUR;
			s.link.write_host_state(host);
		}
	}

	ClientSession& client_session()
	{
		static ClientSession session;
		return session;
	}

	void client_create_move(CUserCmd* cmd)
	{
		cmd->hc_flags = 0;
		auto& s = client_session();
		if (!s.minecraft_owns_player) {
			return;
		}
		// minecraft moves the player; of source's own controls only "use" is left (G or source's +use
		// key), and throwing or dropping what it carries
		cmd->forwardmove = 0.0f;
		cmd->sidemove = 0.0f;
		cmd->upmove = 0.0f;
		cmd->buttons = (s.use_held ? IN_USE : 0) | (s.attack_held ? IN_ATTACK : 0) | (s.attack2_held ? IN_ATTACK2 : 0);
		cmd->viewangles.Init(s.pitch, mc_yaw_to_source(s.yaw), 0.0f);
		if (!s.puppeting || !s.pose_valid) {
			return;  // arriving: the player stays where source put them
		}

		float feet[3];
		mc_to_source(s.pose.feet[0], s.pose.feet[1], s.pose.feet[2], s.slot, feet);
		cmd->hc_origin.Init(feet[0], feet[1], feet[2]);

		// velocity over minecraft's last tick (npc aim, sounds and pushers read it)
		const float tick_seconds = (s.mc.tickMs > 0.0f ? s.mc.tickMs : 50.0f) / 1000.0f;
		const float units_per_second = static_cast<float>(UNITS_PER_BLOCK) / tick_seconds;
		cmd->hc_velocity.Init(static_cast<float>(s.mc.curX - s.mc.prevX) * units_per_second, static_cast<float>(-(s.mc.curZ - s.mc.prevZ)) * units_per_second,
			static_cast<float>(s.mc.curY - s.mc.prevY) * units_per_second);

		cmd->hc_flags = HC_CMD_PUPPET;
		if (s.mc.flags & proto::kMcOnGround) {
			cmd->hc_flags |= HC_CMD_ON_GROUND;
		}
		if (s.mc.eyeHeight > 0.0f && s.mc.eyeHeight < 1.5f) {
			cmd->hc_flags |= HC_CMD_LOW_POSE;  // sneaking (1.27), swimming or crawling (0.4)
		}
		if (s.forward_held && !s.mc_screen_open) {
			cmd->hc_flags |= HC_CMD_FORWARD;
		}
		s.last_set[0] = feet[0], s.last_set[1] = feet[1], s.last_set[2] = feet[2];
		s.have_last_set = true;
	}

	void client_override_view(CViewSetup* setup)
	{
		auto& s = client_session();
		if (!s.puppeting || !s.pose_valid) {
			return;
		}

		// minecraft's walk bob (GameRenderer.bobView) as a camera offset: sway sideways, lift, and dip
		// and roll the view.
		const float phase = s.pose.bob_phase * PI;
		const float bob = s.pose.bob_amount;
		const float side_blocks = -std::sin(phase) * bob * 0.5f;
		const float lift_blocks = std::fabs(std::cos(phase) * bob);
		const float bob_pitch = std::fabs(std::cos(phase - 0.2f) * bob) * 5.0f;
		const float bob_roll = std::sin(phase) * bob * 3.0f;

		const float yaw = mc_yaw_to_source(s.yaw);
		const float yaw_rad = yaw * (PI / 180.0f);
		const float right[2] = { std::sin(yaw_rad), -std::cos(yaw_rad) };

		float eye[3];
		mc_to_source(s.pose.eye[0], s.pose.eye[1], s.pose.eye[2], s.slot, eye);
		const float units = static_cast<float>(UNITS_PER_BLOCK);
		eye[0] += right[0] * side_blocks * units;
		eye[1] += right[1] * side_blocks * units;
		eye[2] += lift_blocks * units;

		// minecraft's F5 camera: behind the player, or in front looking back at them, pulled in
		// wherever minecraft's own zoom collision stopped it (its blocks and half-life's collision).
		// it pulls in at once and eases back out, so a ray grazing the ground can't shake it.
		const bool  detached = s.mc.cameraMode != 0 && s.mc.cameraDistance > 0.0f;
		const bool  mirrored = s.mc.cameraMode == 2;
		const float view_yaw = mirrored ? yaw + 180.0f : yaw;
		const float look_pitch = mirrored ? -s.pitch : s.pitch;
		if (!detached || s.camera_zoom_mode != s.mc.cameraMode || s.mc.cameraDistance < s.camera_zoom) {
			s.camera_zoom = detached ? s.mc.cameraDistance : 0.0f;
		} else {
			s.camera_zoom += (s.mc.cameraDistance - s.camera_zoom) * (1.0f - std::exp(-std::max(gpGlobals->frametime, 0.0f) / CAMERA_EASE_SECONDS));
		}
		s.camera_zoom_mode = s.mc.cameraMode;
		if (detached) {
			// along the look without the bob: minecraft tilts the view for the bob around the camera
			// itself, not by swinging the camera around the head
			Vector forward;
			AngleVectors(QAngle(look_pitch, view_yaw, 0.0f), &forward);
			for (int k = 0; k < 3; ++k) {
				eye[k] -= forward[k] * s.camera_zoom * units;
			}
		}

		setup->origin.Init(eye[0], eye[1], eye[2]);
		setup->angles.Init(look_pitch + bob_pitch, view_yaw, bob_roll);
		if (s.mc.fovDeg > 1.0f) {
			setup->fov = mc_fov_to_source(s.mc.fovDeg);
		}
	}
}

namespace
{
	void queue_server_input(halfcraft::proto::InputType type, int code, std::int32_t a, std::int32_t b = 0, std::int32_t c = 0)
	{
		auto&                       session = halfcraft::client_session();
		std::lock_guard<std::mutex> lock(session.server_inputs_lock);
		session.server_inputs.push_back({ static_cast<std::uint16_t>(type), static_cast<std::uint16_t>(code), a, b, c });
	}
}

// server.dll's way to put half-life's hits and heals on the player onto the input ring (hc_bridge.h)
extern "C" __declspec(dllexport) void HalfCraft_PushHurt(int kind, float damage, std::uint32_t attacker, std::uint32_t flags)
{
	queue_server_input(halfcraft::proto::kInHurt, kind, static_cast<std::int32_t>(damage * 100.0f), static_cast<std::int32_t>(attacker),
		static_cast<std::int32_t>(flags));
}

extern "C" __declspec(dllexport) void HalfCraft_PushHeal(int kind, float amount)
{
	queue_server_input(halfcraft::proto::kInHeal, kind, static_cast<std::int32_t>(amount * 100.0f));
}

// server.dll: the player carries a prop with use, or stopped (hc_bridge.h)
extern "C" __declspec(dllexport) void HalfCraft_SetHolding(int holding)
{
	auto& session = halfcraft::client_session();
	if (session.holding && !holding) {
		session.attack_held = false;  // thrown or dropped: the buttons go back to minecraft
		session.attack2_held = false;
	}
	session.holding = holding != 0;
}

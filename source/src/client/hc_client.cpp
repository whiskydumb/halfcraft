// client.dll: the per-frame link to minecraft. who drives the player, the teleport handshake,
// the host state minecraft reads, and the camera. ported from SkyCraft's Game.cpp.

#include "cbase.h"
#include "c_baseplayer.h"
#include "iclientvehicle.h"
#include "iinput.h"
#include "ienginevgui.h"
#include "in_buttons.h"
#include "usercmd.h"
#include "view.h"
#include "view_shared.h"
#include "ivrenderview.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>

#include "client/hc_block_lights.h"
#include "client/hc_blocks.h"
#include "client/hc_client.h"
#include "client/hc_jumps.h"
#include "shared/hc_floors.h"
#include "client/hc_things.h"
#include "client/hc_weapons.h"
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
		constexpr float CAMERA_HULL_UNITS = 4.0f;    // the F5 camera's half size against walls (minecraft's 0.1 block)

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
		// the ones where minecraft keeps its hands: source only moves the player
		constexpr char TAKEOVER_LADDER[] = "ladder or noclip";
		constexpr char TAKEOVER_RIDING[] = "riding a lift or train";
		// source's alone, but minecraft's F5 camera still goes round it
		constexpr char TAKEOVER_VEHICLE[] = "in a vehicle";

		/// why source keeps the player to itself right now (nullptr: it doesn't).
		const char* source_takeover(C_BasePlayer* player, bool riding)
		{
			if (player->GetVehicle()) {
				return TAKEOVER_VEHICLE;
			}
			if (player->GetObserverMode() != OBS_MODE_NONE) {
				return TAKEOVER_OBSERVING;
			}
			if (player->GetMoveType() != MOVETYPE_WALK) {
				return TAKEOVER_LADDER;
			}
			if (player->GetFlags() & (FL_FROZEN | FL_ATCONTROLS)) {
				return "frozen by the map";
			}
			if (render->GetViewEntity() != player->entindex()) {
				return TAKEOVER_CAMERA;
			}
			if (riding) {
				return TAKEOVER_RIDING;
			}
			return nullptr;
		}

		/// the way the seat of the player's vehicle faces (source degrees): its feet attachment, where
		/// source seats the player (CBaseServerVehicle::GetPassengerSeatPoint), or the vehicle itself.
		bool seat_yaw(C_BasePlayer* player, float& yaw)
		{
			IClientVehicle* vehicle = player->GetVehicle();
			C_BaseEntity*   entity = vehicle ? vehicle->GetVehicleEnt() : nullptr;
			if (!entity) {
				return false;
			}
			QAngle           angles = entity->GetAbsAngles();
			C_BaseAnimating* animating = entity->GetBaseAnimating();
			char             feet[32];
			V_snprintf(feet, sizeof(feet), "vehicle_feet_passenger%d", std::max(vehicle->GetPassengerRole(player), 0));
			const int attachment = animating ? animating->LookupAttachment(feet) : -1;
			Vector    origin;
			if (attachment > 0) {
				animating->GetAttachment(attachment, origin, angles);
			}
			yaw = angles.y;
			return true;
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
				// our game folder runs as half-life 2 (or hl2:dm) and must never trade cfg/config.cfg with
				// that game's steam cloud: the player's own binds (E for use) would take minecraft's keys,
				// and ours would land in their half-life 2. valve.rc says so too, but only after the engine
				// has synced; the game systems start before that
				ConVarRef cloud_settings("cl_cloud_settings");
				if (cloud_settings.IsValid()) {
					cloud_settings.SetValue(0);
				}
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
					s.slot = { map_slot(level), map_grid_z(level) };
					log_info("map %s (slot %d)", level, s.slot.index);
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
				for (const auto& queued : s.server_inputs) {
					s.link.push_input(static_cast<proto::InputType>(queued.type), queued.code, queued.a, queued.b, queued.c);
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
			} else if (s.puppeting && jump_landing_elsewhere()) {
				// source took minecraft's jump next to where minecraft put its player, or refused it and kept
				// the player: once source's is there, minecraft's goes there too
				bool refused = false;
				if (jump_landed_elsewhere(origin, refused)) {
					log_info(refused ? "source refused minecraft's jump: no room for the player there; resyncing minecraft"
									 : "source landed minecraft's jump next to where minecraft put its player; resyncing minecraft");
					s.teleport_pending = true;
					s.have_last_set = false;
				}
			} else if (s.puppeting && s.have_last_set && !jump_in_flight() && distance(origin, s.last_set) > TELEPORT_THRESHOLD) {
				// during one of minecraft's jumps source says itself where the player went (above)
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
			// meanwhile the view is source's: minecraft's player looks the same way (its hands still work)
			if (takeover) {
				const QAngle& view = MainViewAngles();
				s.yaw = source_yaw_to_mc(view.y);
				s.pitch = view.x;
			}

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

			s.minecraft_hands = s.have_mc && s.mc_in_world && !s.source_menu_open && alive && (takeover == TAKEOVER_LADDER || takeover == TAKEOVER_RIDING);
			s.minecraft_camera = s.minecraft_hands || (s.have_mc && s.mc_in_world && !s.source_menu_open && alive && takeover == TAKEOVER_VEHICLE);
			// a screen minecraft's hands open there (a chest, a crafting table) has the keys and the mouse until it closes
			const bool owns_input = s.have_mc && s.mc_in_world && !s.source_menu_open && alive && (!takeover || (s.minecraft_hands && s.mc_screen_open));
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
			if (takeover && takeover != TAKEOVER_OBSERVING && alive && !s.loading) {
				host.flags |= proto::kHostTakeover;  // minecraft's player stays where source's is
			}
			float seat = 0.0f;
			s.seated = takeover == TAKEOVER_VEHICLE && seat_yaw(player, seat);
			if (s.seated) {
				host.flags |= proto::kHostSeated;  // its body sits in the seat in third person
				host.seatYaw = source_yaw_to_mc(seat);
			}
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
			host.speedFactor = player ? player->GetLaggedMovementValue() : 1.0f;  // player_speedmod
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
		weapon_create_move(s, cmd);
		if (!s.puppeting) {
			jump_forget(s);
		}
		if (!s.minecraft_owns_player) {
			return;
		}
		// minecraft moves the player; of source's own controls only "use" is left (G or source's +use
		// key), throwing or dropping what it carries, and the half-life weapon minecraft holds
		cmd->forwardmove = 0.0f;
		cmd->sidemove = 0.0f;
		cmd->upmove = 0.0f;
		cmd->buttons = (s.use_held ? IN_USE : 0) | weapon_buttons(s);
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

		cmd->hc_flags |= HC_CMD_PUPPET | jump_flags(s, feet);  // on top of weapon_create_move's
		if (s.mc.flags & proto::kMcOnGround) {
			cmd->hc_flags |= HC_CMD_ON_GROUND;
		}
		if (s.mc.eyeHeight > 0.0f && s.mc.eyeHeight < 1.5f) {
			cmd->hc_flags |= HC_CMD_LOW_POSE;  // crouching (0.7: source's duck, HostDuck), swimming or crawling (0.4)
		}
		if (s.forward_held && !s.mc_screen_open) {
			cmd->hc_flags |= HC_CMD_FORWARD;
		}
		s.last_set[0] = feet[0], s.last_set[1] = feet[1], s.last_set[2] = feet[2];
		s.have_last_set = true;
	}

	namespace
	{
		/// how far back from the eye minecraft's camera gets in a vehicle (blocks). minecraft's zoom leaves
		/// half-life's collision out there (CameraZoomMixin): the vehicle round the seat, which minecraft
		/// sees as walls, would stop it at the head, as a minecraft boat doesn't. so it stops here, at
		/// whatever of half-life's is in the way besides the vehicle.
		float seated_camera_reach(C_BasePlayer* player, const float* eye, const Vector& forward, float blocks)
		{
			IClientVehicle*            vehicle = player->GetVehicle();
			CTraceFilterSkipTwoEntities filter(player, vehicle ? vehicle->GetVehicleEnt() : nullptr, COLLISION_GROUP_NONE);
			const Vector               from(eye[0], eye[1], eye[2]);
			const Vector               hull(CAMERA_HULL_UNITS, CAMERA_HULL_UNITS, CAMERA_HULL_UNITS);
			trace_t                    trace;
			UTIL_TraceHull(from, from - forward * (blocks * static_cast<float>(UNITS_PER_BLOCK)), -hull, hull, MASK_SOLID & ~CONTENTS_MONSTER, &filter, &trace);
			return blocks * trace.fraction;
		}

		/// minecraft's F5 camera: behind the player, or in front looking back at them, pulled in
		/// wherever minecraft's own zoom collision stopped it (its blocks and half-life's collision).
		/// it pulls in at once and eases back out, so a ray grazing the ground can't shake it.
		/// @param eye - the first-person eye, moved back to the camera
		/// @param pitch, yaw - the look, turned round for the camera in front
		/// @param seated - the player in a vehicle's seat (@ref seated_camera_reach), or nullptr
		void detach_camera(ClientSession& s, float* eye, float& pitch, float& yaw, C_BasePlayer* seated = nullptr)
		{
			const bool detached = s.mc.cameraMode != 0 && s.mc.cameraDistance > 0.0f;
			if (s.mc.cameraMode == 2) {
				yaw += 180.0f;
				pitch = -pitch;
			}
			Vector forward;
			AngleVectors(QAngle(pitch, yaw, 0.0f), &forward);
			float reach = detached ? s.mc.cameraDistance : 0.0f;
			if (detached && seated) {
				reach = seated_camera_reach(seated, eye, forward, reach);
			}
			if (!detached || s.camera_zoom_mode != s.mc.cameraMode || reach < s.camera_zoom) {
				s.camera_zoom = reach;
			} else {
				s.camera_zoom += (reach - s.camera_zoom) * (1.0f - std::exp(-std::max(gpGlobals->frametime, 0.0f) / CAMERA_EASE_SECONDS));
			}
			s.camera_zoom_mode = s.mc.cameraMode;
			if (detached) {
				for (int k = 0; k < 3; ++k) {
					eye[k] -= forward[k] * s.camera_zoom * static_cast<float>(UNITS_PER_BLOCK);
				}
			}
		}
	}

	void client_override_view(CViewSetup* setup)
	{
		auto& s = client_session();
		s.have_takeover_eye = !s.puppeting && s.minecraft_camera && s.pose_valid;
		if (s.have_takeover_eye) {
			// a ladder, a ride or a vehicle: source's own view (a vehicle's seat), with minecraft's F5 round it
			float eye[3] = { setup->origin.x, setup->origin.y, setup->origin.z };
			std::copy(eye, eye + 3, s.takeover_eye);
			float pitch = setup->angles.x, yaw = setup->angles.y;
			detach_camera(s, eye, pitch, yaw, s.seated ? C_BasePlayer::GetLocalPlayer() : nullptr);
			setup->origin.Init(eye[0], eye[1], eye[2]);
			setup->angles.Init(pitch, yaw, setup->angles.z);
			return;
		}
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

		// minecraft's F5 camera, along the look without the bob: minecraft tilts the view for the bob
		// around the camera itself, not by swinging the camera around the head
		float look_pitch = s.pitch, view_yaw = yaw;
		if (s.mc.flags & proto::kMcSleeping) {
			// in a bed minecraft holds the look (level, and in first person along the bed from just above
			// the pillow), and the F5 camera goes round that look
			mc_to_source(s.mc.eyeX, s.mc.eyeY, s.mc.eyeZ, s.slot, eye);
			look_pitch = s.mc.pitch;
			view_yaw = mc_yaw_to_source(s.mc.yaw);
		}
		detach_camera(s, eye, look_pitch, view_yaw);

		setup->origin.Init(eye[0], eye[1], eye[2]);
		setup->angles.Init(look_pitch + bob_pitch, view_yaw, bob_roll);
		if (s.mc.fovDeg > 1.0f) {
			setup->fov = weapon_zoom_fov(mc_fov_to_source(s.mc.fovDeg));
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

extern "C" __declspec(dllexport) void HalfCraft_PushInput(int type, int code, int a, int b, int c)
{
	queue_server_input(static_cast<halfcraft::proto::InputType>(type), code, a, b, c);
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
	if (session.holding != (holding != 0)) {
		halfcraft::log_info(holding ? "carrying a prop: the left mouse button throws it, the right one drops it" : "no longer carrying a prop");
	}
	session.holding = holding != 0;
}

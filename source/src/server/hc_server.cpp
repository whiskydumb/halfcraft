// server.dll: streams half-life 2's collision to minecraft and hides half-life's own hud and
// weapons while minecraft drives the player. the client writes the host state; this side only
// reads it (for the collision epoch and whether a map is loaded).

#include "cbase.h"
#include "player.h"
#include "in_buttons.h"
#include "hl2/weapon_physcannon.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <memory>

#include "core/hc_collision.h"
#include "core/hc_link.h"
#include "core/hc_log.h"
#include "core/hc_module.h"
#include "server/hc_blast.h"
#include "server/hc_block_solids.h"
#include "server/hc_checkpoints.h"
#include "server/hc_combat.h"
#include "server/hc_debug_target.h"
#include "server/hc_hazards.h"
#include "server/hc_mobs.h"
#include "server/hc_push.h"
#include "server/hc_vitals.h"
#include "server/hc_weapons.h"
#include "server/hc_world_collision.h"
#include "shared/hc_bridge.h"
#include "shared/hc_floors.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		// half-life's hud parts minecraft replaces while it owns the player's health (also on ladders
		// and rides): its health/suit, weapon selection and crosshair. the flashlight meter stays (V
		// still toggles the flashlight).
		constexpr int   HIDDEN_HUD = HIDEHUD_HEALTH | HIDEHUD_WEAPONSELECTION | HIDEHUD_CROSSHAIR;
		constexpr float PUPPET_GRACE_SECONDS = 0.5f;

		ConVar hc_debug_use("hc_debug_use", "0", 0, "halfcraft: log what half-life's use finds whenever it's pressed");

		float g_last_puppet_move = -1.0f;

		void console_sink(int severity, const char* line)
		{
			if (severity == LOG_INFO) {
				Msg("%s\n", line);
			} else {
				Warning("%s\n", line);
			}
		}

		class HalfCraftServerSystem final : public CAutoGameSystemPerFrame
		{
		public:
			HalfCraftServerSystem() : CAutoGameSystemPerFrame("HalfCraftServer") {}

			bool Init() override
			{
				set_log_sink(console_sink);
				link_ready_ = link_.attach();
				if (link_ready_) {
					streamer_ = std::make_unique<CollisionStreamer>(link_);
				}
				return true;
			}

			void Shutdown() override
			{
				streamer_.reset();
				set_log_sink(nullptr);
			}

			void LevelInitPostEntity() override
			{
				slot_ = { map_slot(STRING(gpGlobals->mapname)), map_grid_z(STRING(gpGlobals->mapname)) };
				world_.reset(slot_, STRING(gpGlobals->mapname));
				solids_.reset(slot_);
				combat_.reset(slot_);
				half_life_blasts_reset(slot_);
				vitals_.reset();
				checkpoints_.on_level_loaded();
				g_last_puppet_move = -1.0f;  // the clock starts over with the map
			}

			// a save is being written (its entities come next)
			void OnSave() override { checkpoints_.on_save(); }

			void LevelShutdownPreEntity() override
			{
				world_.reset({}, nullptr);
				mobs_.reset();
			}

			void FrameUpdatePostEntityThink() override;

			/// hc_debug_voxels
			void check_voxels(int radius)
			{
				CBasePlayer* player = UTIL_GetLocalPlayer();
				if (!player) {
					log_info("hc_debug_voxels: no player");
					return;
				}
				const Vector origin = player->GetAbsOrigin();
				const float  feet[3] = { origin.x, origin.y, origin.z };
				world_.check_voxels(source_to_mc(feet, slot_), radius);
			}

		private:
			void update_hud(CBasePlayer* player, bool hide);
			void update_use(CBasePlayer* player);

			Link                               link_;
			bool                               link_ready_ = false;
			std::unique_ptr<CollisionStreamer> streamer_;
			WorldCollision                     world_;
			BlockSolids                        solids_;
			Weapons                            weapons_;
			Combat                             combat_;
			Mobs                               mobs_;
			Push                               push_;
			DebugTarget                        debug_target_;
			Vitals                             vitals_;
			Hazards                            hazards_;
			Checkpoints                        checkpoints_;
			MapSlot                            slot_;
			std::uint32_t                      epoch_ = ~0u;
		};

		HalfCraftServerSystem g_server_system;

		void HalfCraftServerSystem::FrameUpdatePostEntityThink()
		{
			if (!link_ready_ || !streamer_) {
				return;
			}
			solids_.update();  // minecraft's blocks stop npcs, props and bullets

			CBasePlayer* player = UTIL_GetLocalPlayer();
			const bool   puppeted = player && server_player_puppeted();
			if (player) {
				update_hud(player, minecraft_owns_health());
				update_use(player);
			}
			weapons_.update(link_, player, minecraft_owns_health());
			// minecraft's weapons work on ladders, rides and in vehicles too: only the movement is source's
			combat_.update(link_, player, slot_, minecraft_owns_health());
			push_.update(player, puppeted && link_.mc_alive());
			mobs_.update(link_, slot_);
			vitals_.update(link_, player);
			if (link_.mc_alive()) {
				hazards_.update(player, slot_);
			}
			debug_target_.update(link_, player);

			proto::HostState host{};
			if (!link_.read_host_state(host)) {
				return;
			}
			if (host.collisionEpoch != epoch_) {
				epoch_ = host.collisionEpoch;
				streamer_->reset(epoch_);
			}
			const bool in_game = (host.flags & proto::kHostInGame) && !(host.flags & proto::kHostLoading);
			if (!player || !in_game || !link_.mc_alive()) {
				return;
			}

			// collision streams around where minecraft's player is (the same as source's while puppeted,
			// and where minecraft is heading after a teleport otherwise), and under its projectiles that
			// fell out of the bottom of that
			for (const McVec& point : combat_.take_collision_wanted()) {
				streamer_->want_below(point);
			}
			const McVec centre{ host.posX, host.posY, host.posZ };
			world_.track_movers(centre, *streamer_);
			streamer_->update(centre, world_);
		}

		void HalfCraftServerSystem::update_use(CBasePlayer* player)
		{
			// the client sends the mouse to source while a prop is carried (throw, drop)
			static SetHoldingFn set_holding = reinterpret_cast<SetHoldingFn>(find_export("client.dll", HC_SET_HOLDING_EXPORT));
			if (set_holding) {
				set_holding(GetPlayerHeldEntity(player) != nullptr ? 1 : 0);
			}
			if (hc_debug_use.GetBool() && (player->m_afButtonPressed & IN_USE)) {
				CBaseEntity* target = player->FindUseEntity();
				log_info("use: %s%s", target ? target->GetClassname() : "nothing in reach", GetPlayerHeldEntity(player) ? " (carrying)" : "");
			}
		}

		void HalfCraftServerSystem::update_hud(CBasePlayer* player, bool hide)
		{
			// the player's own flags, not a copy of them: a save brings back whatever it was saved with.
			// the viewmodel is the weapons' (hc_weapons.cpp)
			const bool hidden = (player->m_Local.m_iHideHUD & HIDDEN_HUD) == HIDDEN_HUD;
			if (hide == hidden) {
				return;
			}
			if (hide) {
				player->m_Local.m_iHideHUD |= HIDDEN_HUD;
			} else {
				player->m_Local.m_iHideHUD &= ~HIDDEN_HUD;
			}
		}
	}

	void server_note_puppet_move()
	{
		g_last_puppet_move = gpGlobals->curtime;
	}

	bool server_player_puppeted()
	{
		return g_last_puppet_move >= 0.0f && gpGlobals->curtime - g_last_puppet_move < PUPPET_GRACE_SECONDS;
	}
}

CON_COMMAND(hc_debug_voxels, "halfcraft: check minecraft's collision voxels around the player against source's own collision: hc_debug_voxels [regions around, 0-6, default 2]")
{
	if (!UTIL_IsCommandIssuedByServerAdmin()) {
		return;
	}
	const int radius = args.ArgC() > 1 ? std::clamp(atoi(args[1]), 0, 6) : 2;
	halfcraft::g_server_system.check_voxels(radius);
}

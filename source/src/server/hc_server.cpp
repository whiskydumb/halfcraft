// server.dll: streams half-life 2's collision to minecraft and hides half-life's own hud and
// weapons while minecraft drives the player. the client writes the host state; this side only
// reads it (for the collision epoch and whether a map is loaded).

#include "cbase.h"
#include "player.h"
#include "in_buttons.h"
#include "hl2/weapon_physcannon.h"

#include "tier0/valve_minmax_off.h"
#include <memory>

#include "core/hc_collision.h"
#include "core/hc_link.h"
#include "core/hc_log.h"
#include "core/hc_module.h"
#include "server/hc_block_solids.h"
#include "server/hc_checkpoints.h"
#include "server/hc_combat.h"
#include "server/hc_hazards.h"
#include "server/hc_vitals.h"
#include "server/hc_world_collision.h"
#include "shared/hc_bridge.h"
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
		constexpr int HIDDEN_HUD = HIDEHUD_HEALTH | HIDEHUD_WEAPONSELECTION | HIDEHUD_CROSSHAIR;
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
				slot_ = map_slot(STRING(gpGlobals->mapname));
				world_.reset(slot_);
				solids_.reset(slot_);
				vitals_.reset();
				checkpoints_.on_level_loaded();
				g_last_puppet_move = -1.0f;  // the clock starts over with the map
			}

			// a save is being written (its entities come next)
			void OnSave() override { checkpoints_.on_save(); }

			void LevelShutdownPreEntity() override
			{
				world_.reset(0);
			}

			void FrameUpdatePostEntityThink() override;

		private:
			void update_hud(CBasePlayer* player, bool hide);
			void update_use(CBasePlayer* player);

			Link                               link_;
			bool                               link_ready_ = false;
			std::unique_ptr<CollisionStreamer> streamer_;
			WorldCollision                     world_;
			BlockSolids                        solids_;
			Combat                             combat_;
			Vitals                             vitals_;
			Hazards                            hazards_;
			Checkpoints                        checkpoints_;
			int                                slot_ = 0;
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
			combat_.update(link_, player, slot_, puppeted && link_.mc_alive());
			vitals_.update(link_, player);
			if (link_.mc_alive()) {
				hazards_.update(player, slot_);
			}

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
			// and where minecraft is heading after a teleport otherwise)
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
			// the player's own flags, not a copy of them: a save brings back whatever it was saved with
			const bool hidden = (player->m_Local.m_iHideHUD & HIDDEN_HUD) == HIDDEN_HUD && !player->m_Local.m_bDrawViewmodel;
			if (hide == hidden) {
				return;
			}
			if (hide) {
				player->m_Local.m_iHideHUD |= HIDDEN_HUD;
				player->ShowViewModel(false);
			} else {
				player->m_Local.m_iHideHUD &= ~HIDDEN_HUD;
				player->ShowViewModel(true);
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

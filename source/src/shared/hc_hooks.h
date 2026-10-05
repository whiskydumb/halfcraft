#pragma once

// the few places where valve's game code calls into halfcraft (see source/sdk/halfcraft-<engine>.patch). everything
// else halfcraft does runs from its own game systems.

#include "inputsystem/ButtonCode.h"

class CBasePlayer;
class CTakeDamageInfo;
class CUserCmd;
class CViewSetup;
class QAngle;

namespace halfcraft
{
	/// CUserCmd::hc_flags
	enum UserCmdFlag : int
	{
		HC_CMD_PUPPET = 1 << 0,     // minecraft moved the player: hc_origin and hc_velocity are set
		HC_CMD_ON_GROUND = 1 << 1,  // minecraft's player stands on something
		HC_CMD_LOW_POSE = 1 << 2,   // sneaking, crawling or swimming: source uses its ducked hull
		HC_CMD_FORWARD = 1 << 3,    // minecraft's forward key is held (walking into a ladder mounts it)
	};

#ifdef CLIENT_DLL
	/// minecraft's lights that cast shadows (hc_block_lights.cpp), each six faces whose light walls stop
	/// and six whose light they don't, all with a shadow depth texture: on top of the one for the
	/// player's flashlight (CClientShadowMgr::Init).
	inline constexpr int SHADOWED_LIGHTS = 4;
	inline constexpr int SHADOWED_FACES = 12 * SHADOWED_LIGHTS;

	/// ClientModeShared::KeyInput.
	/// @return false to swallow the key (it went to minecraft)
	bool client_key_event(int down, ButtonCode_t code, const char* binding);

	/// CInput::MouseMove, with the raw accumulated mouse counts of this sample.
	/// @param view_angles - set to minecraft's look when halfcraft owns the view
	/// @return true when halfcraft took the mouse (source must not apply it)
	bool client_mouse_move(float mouse_x, float mouse_y, QAngle& view_angles);

	/// ClientModeShared::CreateMove: puts minecraft's player position into the command.
	void client_create_move(CUserCmd* cmd);

	/// ClientModeShared::OverrideView: the camera sits where minecraft's player looks from.
	void client_override_view(CViewSetup* setup);

	/// CHLClient::HandleUiToggle (esc).
	/// @return true when esc went to minecraft (closing its screen) instead of opening source's menu
	bool client_ui_toggle();

	/// CClientShadowMgr::ComputeShadowDepthTextures, before a projected texture's shadow depth texture
	/// is drawn.
	/// @param shadow - its ClientShadowHandle_t
	void client_shadow_depth_begin(unsigned short shadow);

	/// CShadowDepthView::Draw: whether that shadow depth texture gets half-life's scene.
	/// @return false for one that holds nothing but client_shadow_depth_view's cap
	bool client_shadow_depth_scene();

	/// CShadowDepthView::Draw, after the scene went into a projected texture's shadow depth texture.
	void client_shadow_depth_view(const CViewSetup& view);
#endif

#ifdef GAME_DLL
	/// CGameMovement::HalfCraftMove ran for the player this tick.
	void server_note_puppet_move();
	/// the player's latest commands came from minecraft.
	bool server_player_puppeted();

	/// CBasePlayer::OnTakeDamage.
	/// @return true when the hit went to minecraft's health instead (source must not apply it)
	bool server_player_damage(CBasePlayer* player, const CTakeDamageInfo& info);
#endif
}

#pragma once

// the few places where valve's game code calls into halfcraft (see source/sdk/halfcraft-<engine>.patch). everything
// else halfcraft does runs from its own game systems.

#include "inputsystem/ButtonCode.h"

class CBaseCombatWeapon;
class CBaseEntity;
class CBasePlayer;
class CTakeDamageInfo;
class CUserCmd;
class CViewSetup;
class QAngle;
class Vector;
struct client_textmessage_t;

namespace halfcraft
{
	/// CUserCmd::hc_flags
	enum UserCmdFlag : int
	{
		HC_CMD_PUPPET = 1 << 0,     // minecraft moved the player: hc_origin and hc_velocity are set
		HC_CMD_ON_GROUND = 1 << 1,  // minecraft's player stands on something
		HC_CMD_LOW_POSE = 1 << 2,   // sneaking, crawling or swimming: source uses its ducked hull
		HC_CMD_FORWARD = 1 << 3,    // minecraft's forward key is held (walking into a ladder mounts it)
		HC_CMD_WEAPONS = 1 << 4,    // minecraft's hand picks the weapon: weaponselect is the one it holds, 0 puts it away
		HC_CMD_VIEWMODEL = 1 << 5,  // minecraft holds the weapon source has out, seen through the player's eyes
		HC_CMD_JUMP = 1 << 6,       // minecraft moved its player by itself (a teleport, a fast dive between two frames):
		                            // source takes hc_origin however far it is, if the player fits there
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

	/// CViewRender::SetUpView, after OverrideView: where the viewmodel is drawn from (hc_weapons.cpp).
	/// @param origin, angles - the viewmodel's eye, source's own until set
	void client_viewmodel_view(CViewSetup& view, Vector& origin, QAngle& angles);

	/// CHLClient::HandleUiToggle (esc).
	/// @return true when esc went to minecraft (closing its screen) instead of opening source's menu
	bool client_ui_toggle();

	/// CClientShadowMgr::ComputeShadowDepthTextures, before a projected texture's shadow depth texture
	/// is drawn.
	/// @param shadow - its ClientShadowHandle_t
	void client_shadow_depth_begin(unsigned short shadow);

	/// CClientShadowMgr::BuildFlashlight: whether a projected texture lights characters too. minecraft's
	/// block lights light only the world: with a dozen of their faces on one npc the engine's shadow
	/// lists broke (an npc hurt or a light changing next to one crashed in RemoveAllShadowsFromModel).
	/// elights light characters instead.
	/// @param shadow - its ClientShadowHandle_t
	/// @return false to light the world only
	bool client_flashlight_lights_models(unsigned short shadow);

	/// CShadowDepthView::Draw: whether that shadow depth texture gets half-life's scene.
	/// @return false for one that holds nothing but client_shadow_depth_view's cap
	bool client_shadow_depth_scene();

	/// CShadowDepthView::Draw, after the scene went into a projected texture's shadow depth texture.
	void client_shadow_depth_view(const CViewSetup& view);

	/// TextMessageGet (cdll_util.cpp): a titles.txt message of the campaign being played
	/// (hc_campaign_text.cpp).
	/// @return nullptr to leave it to the engine
	client_textmessage_t* client_text_message(const char* name);

	/// CHudCredits::PrepareCredits: the search path id of the campaign being played, to read its
	/// scripts/credits.txt from.
	const char* client_campaign_path_id();

	/// ClientModeShared::PostRenderVGui: the frame is finished (the world, the hud and minecraft's
	/// overlay); only source's menus and console still go over it.
	void client_post_render_vgui();

	/// CGameMovement::HalfCraftMove, in prediction, and server.dll's (HC_JUMP_LANDED_EXPORT): source
	/// took one of minecraft's jumps next to where minecraft put its player, which didn't fit (a pearl
	/// against a ceiling, a ledge or a wall), or refused it. minecraft's player goes there too (hc_jumps.cpp).
	/// @param feet - where source put the player, or kept it
	/// @param refused - source refused the jump: no room there or near it
	void client_jump_landed_elsewhere(const Vector& feet, bool refused);
#endif

#ifdef GAME_DLL
	/// CGameMovement::HalfCraftMove ran for the player this tick.
	void server_note_puppet_move();
	/// the player's latest commands came from minecraft.
	bool server_player_puppeted();

	/// CBaseEntity::ApplyAbsVelocityImpulse on a player: a shove that's over at once (a trigger_push that
	/// pushes once, an antlion guard, a cop's stunstick), which minecraft's player takes as momentum while
	/// minecraft drives it: its next move would undo it in source (hc_push.cpp).
	void server_player_impulse(CBaseEntity* player, const Vector& impulse);

	/// around source's own corrections of the player's velocity, which are no shoves: a push that ends
	/// (CPlayerMove::CheckMovingGround; kInPush's end carries it) and the physics shadow keeping up with
	/// what the player stands on (CBasePlayer::VPhysicsShadowUpdate). server_player_impulse ignores what
	/// comes while one is alive.
	struct ServerImpulseQuiet
	{
		ServerImpulseQuiet();
		~ServerImpulseQuiet();
		ServerImpulseQuiet(const ServerImpulseQuiet&) = delete;
		ServerImpulseQuiet& operator=(const ServerImpulseQuiet&) = delete;
	};

	/// CBasePlayer::OnTakeDamage.
	/// @return true when the hit went to minecraft's health instead (source must not apply it)
	bool server_player_damage(CBasePlayer* player, const CTakeDamageInfo& info);

	/// CBasePlayer::Weapon_Equip and BumpWeapon (a weapon picked up) and CHL2_Player::GiveAmmo (ammo
	/// for a dry one), where source would take a weapon out by itself (hc_weapons.cpp).
	/// @return true when minecraft's hand picks the player's weapon instead: that one stays away
	bool server_minecraft_picks_weapon(CBasePlayer* player, CBaseCombatWeapon* weapon);
#endif
}

// CGameMovement::HalfCraftMove: while minecraft drives the player, its physics have already moved
// them and the command carries the result. runs on the server and in client prediction alike, so
// both end up at the same spot.

#include "cbase.h"
#include "gamemovement.h"
#include "in_buttons.h"
#include "movevars_shared.h"
#ifdef GAME_DLL
#include "env_player_surface_trigger.h"
#endif

#include "core/hc_log.h"
#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// units; minecraft's fastest fall covers about 50 per command
static const float HALFCRAFT_MAX_STEP = 160.0f;

// minecraft's player is 0.6 blocks wide, source's hull 0.8: minecraft stands it closer to walls than
// source's hull fits, so the box tests here use minecraft's width
static const float HALFCRAFT_HALF_WIDTH = 12.0f;

// a box resting exactly on a floor starts solid in it, and the server gets the command's origin cut
// to 1/32 unit, a hair into the floor: the box tests start this far up
static const float HALFCRAFT_LIFT = 1.0f;

// how far below the feet the ground is looked for (CGameMovement::CategorizePosition's 2), and how
// tall the box looking for it is
static const float HALFCRAFT_GROUND_DROP = 2.0f;
static const float HALFCRAFT_GROUND_PROBE = 4.0f;

// where minecraft's jump ends its player may lean into a wall or hang over a ledge (a pearl lands
// against a wall): room half its width to a side, or a stair step up, is room enough
static const Vector HALFCRAFT_JUMP_NUDGES[] = {
	Vector( 0.0f, 0.0f, 0.0f ),
	Vector( HALFCRAFT_HALF_WIDTH, 0.0f, 0.0f ),
	Vector( -HALFCRAFT_HALF_WIDTH, 0.0f, 0.0f ),
	Vector( 0.0f, HALFCRAFT_HALF_WIDTH, 0.0f ),
	Vector( 0.0f, -HALFCRAFT_HALF_WIDTH, 0.0f ),
	Vector( HALFCRAFT_HALF_WIDTH, HALFCRAFT_HALF_WIDTH, 0.0f ),
	Vector( HALFCRAFT_HALF_WIDTH, -HALFCRAFT_HALF_WIDTH, 0.0f ),
	Vector( -HALFCRAFT_HALF_WIDTH, HALFCRAFT_HALF_WIDTH, 0.0f ),
	Vector( -HALFCRAFT_HALF_WIDTH, -HALFCRAFT_HALF_WIDTH, 0.0f ),
	Vector( 0.0f, 0.0f, 18.0f ),
};

// a lift, train or other pusher under the player is moving. source carries riders along with it;
// minecraft can't, so source keeps the player until it stops (the client sees the ride too and
// hands source the controls). only the server knows a pusher's velocity.
static bool HalfCraftRiding( CBasePlayer *pPlayer )
{
#ifdef GAME_DLL
	CBaseEntity *pGround = pPlayer->GetGroundEntity();
	return pGround && pGround->GetMoveType() == MOVETYPE_PUSH &&
		   ( pGround->GetAbsVelocity().LengthSqr() > 1.0f || pGround->GetLocalAngularVelocity().LengthSqr() > 1.0f );
#else
	return false;
#endif
}

// minecraft's jump lands its player where it fits: its box (minecraft's width, source's height for
// the pose) is clear of everything the player collides with there, or a nudge away from it
static bool HalfCraftFits( CGameMovement *pMovement, const Vector &vecOrigin, bool bLow )
{
	const Vector vecMins( -HALFCRAFT_HALF_WIDTH, -HALFCRAFT_HALF_WIDTH, HALFCRAFT_LIFT );
	const Vector vecMaxs( HALFCRAFT_HALF_WIDTH, HALFCRAFT_HALF_WIDTH, pMovement->GetPlayerMaxs( bLow ).z );
	for ( const Vector &vecNudge : HALFCRAFT_JUMP_NUDGES )
	{
		const Vector vecAt = vecOrigin + vecNudge;
		trace_t pm;
		pMovement->TryTouchGround( vecAt, vecAt, vecMins, vecMaxs, pMovement->PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
		if ( !pm.startsolid )
			return true;
	}
	return false;
}

#ifdef GAME_DLL
// a jump this long gets a line; shorter flagged moves are minecraft walking on right after one
static const float HALFCRAFT_JUMP_LOG_UNITS = 40.0f;
// a fast dive jumps every frame: a line a second is plenty
static const float HALFCRAFT_JUMP_LOG_SECONDS = 1.0f;

static void HalfCraftNoteJump( float flStep, const Vector &vecTo, bool bFits )
{
	static float s_flLogged = -1.0f;
	if ( bFits && flStep < HALFCRAFT_JUMP_LOG_UNITS )
		return;
	// the clock starts over with every map
	if ( s_flLogged >= 0.0f && gpGlobals->curtime >= s_flLogged && gpGlobals->curtime < s_flLogged + HALFCRAFT_JUMP_LOG_SECONDS )
		return;
	s_flLogged = gpGlobals->curtime;
	if ( bFits )
		halfcraft::log_info( "minecraft's jump of %.0f units: source takes the player to (%.0f %.0f %.0f)", flStep, vecTo.x, vecTo.y, vecTo.z );
	else
		halfcraft::log_info( "minecraft's jump of %.0f units refused: the player doesn't fit at (%.0f %.0f %.0f)", flStep, vecTo.x, vecTo.y, vecTo.z );
}
#endif

bool CGameMovement::HalfCraftMove( void )
{
	const CUserCmd *cmd = player->GetCurrentUserCommand();
	if ( !cmd || !( cmd->hc_flags & halfcraft::HC_CMD_PUPPET ) )
		return false;

	// source keeps anything that isn't plain walking (ladders, noclip, vehicles, observing) and riders
	if ( player->GetMoveType() != MOVETYPE_WALK || player->pl.deadflag || HalfCraftRiding( player ) )
		return false;

	// minecraft's pose decides the hull: crouched while sneaking, crawling or swimming
	const bool bLow = ( cmd->hc_flags & halfcraft::HC_CMD_LOW_POSE ) != 0;

	// a command minecraft made before source teleported the player would drag them straight back: no
	// single command moves this far. minecraft's own jumps (its teleports, a fast dive between two
	// frames) say so, and land wherever the player fits; where it doesn't, the client resyncs minecraft
	const float flStep = ( cmd->hc_origin - mv->GetAbsOrigin() ).Length();
	if ( cmd->hc_flags & halfcraft::HC_CMD_JUMP )
	{
		const bool bFits = HalfCraftFits( this, cmd->hc_origin, bLow );
#ifdef GAME_DLL
		HalfCraftNoteJump( flStep, cmd->hc_origin, bFits );
#endif
		if ( !bFits )
			return false;
	}
	else if ( flStep > HALFCRAFT_MAX_STEP )
	{
		return false;
	}

	if ( bLow != player->m_Local.m_bDucked )
	{
		player->m_Local.m_bDucked = bLow;
		player->m_Local.m_bDucking = false;
		player->m_Local.m_flDucktime = 0.0f;
		if ( bLow )
			player->AddFlag( FL_DUCKING );
		else
			player->RemoveFlag( FL_DUCKING );
	}

	mv->SetAbsOrigin( cmd->hc_origin );
	mv->m_vecVelocity = cmd->hc_velocity;

	// minecraft takes its own fall damage
	player->m_Local.m_flFallVelocity = 0.0f;

	// what the player stands on (pushers, physics props and npcs care, and its material sets the
	// surface data), and how wet they are. a flat box of minecraft's width from just above the feet:
	// source's wider hull would catch on the walls minecraft's player stands next to
	trace_t pm;
	const Vector vecMins( -HALFCRAFT_HALF_WIDTH, -HALFCRAFT_HALF_WIDTH, 0.0f );
	const Vector vecMaxs( HALFCRAFT_HALF_WIDTH, HALFCRAFT_HALF_WIDTH, HALFCRAFT_GROUND_PROBE );
	const Vector vecUp = cmd->hc_origin + Vector( 0.0f, 0.0f, HALFCRAFT_LIFT );
	const Vector vecDown = cmd->hc_origin - Vector( 0.0f, 0.0f, HALFCRAFT_GROUND_DROP );
	TryTouchGround( vecUp, vecDown, vecMins, vecMaxs, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
	const bool bGround = ( cmd->hc_flags & halfcraft::HC_CMD_ON_GROUND ) && pm.m_pEnt && !pm.startsolid && pm.plane.normal.z >= 0.7f;
	SetGroundEntity( bGround ? &pm : NULL );
	CheckWater();

#ifdef GAME_DLL
	// the ground's material goes to the env_player_surface_triggers maps hang scripted events on (the
	// coast's sand wakes the antlions), as CGameMovement::CategorizePosition tells them
	char chMaterial = 0;
	if ( bGround )
	{
		const surfacedata_t *pSurface = MoveHelper()->GetSurfaceProps()->GetSurfaceData( pm.surface.surfaceProps );
		chMaterial = pSurface ? pSurface->game.material : 0;
	}
	if ( player->m_chPreviousTextureType != chMaterial )
		CEnvPlayerSurfaceTrigger::SetPlayerSurface( player, chMaterial );
	player->m_chPreviousTextureType = chMaterial;
#endif

	// half-life's ladders mount the way half-life mounts them: "use" (G) while looking at one, or
	// walking into one. source then has the player (it slides them on and sets the ladder movetype)
	// until they get off, and minecraft picks up from there.
	if ( !( player->GetFlags() & FL_ONTRAIN ) )
	{
		mv->m_flForwardMove = ( cmd->hc_flags & halfcraft::HC_CMD_FORWARD ) ? MAX( mv->m_flMaxSpeed, 1.0f ) : 0.0f;
		LadderMove();
		mv->m_flForwardMove = 0.0f;
	}

#ifdef GAME_DLL
	halfcraft::server_note_puppet_move();
#endif
	return true;
}

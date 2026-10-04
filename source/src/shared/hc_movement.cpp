// CGameMovement::HalfCraftMove: while minecraft drives the player, its physics have already moved
// them and the command carries the result. runs on the server and in client prediction alike, so
// both end up at the same spot.

#include "cbase.h"
#include "gamemovement.h"
#include "in_buttons.h"
#include "movevars_shared.h"

#include "shared/hc_hooks.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// units; minecraft's fastest fall covers about 50 per command
static const float HALFCRAFT_MAX_STEP = 160.0f;

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

bool CGameMovement::HalfCraftMove( void )
{
	const CUserCmd *cmd = player->GetCurrentUserCommand();
	if ( !cmd || !( cmd->hc_flags & halfcraft::HC_CMD_PUPPET ) )
		return false;

	// source keeps anything that isn't plain walking (ladders, noclip, vehicles, observing) and riders
	if ( player->GetMoveType() != MOVETYPE_WALK || player->pl.deadflag || HalfCraftRiding( player ) )
		return false;

	// a command minecraft made before source teleported the player would drag them straight back:
	// no single command moves this far
	if ( ( cmd->hc_origin - mv->GetAbsOrigin() ).LengthSqr() > HALFCRAFT_MAX_STEP * HALFCRAFT_MAX_STEP )
		return false;

	// minecraft's pose decides the hull: crouched while sneaking, crawling or swimming
	const bool bLow = ( cmd->hc_flags & halfcraft::HC_CMD_LOW_POSE ) != 0;
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

	// what the player stands on (pushers, physics props and npcs care), and how wet they are
	trace_t pm;
	const Vector vecDown = cmd->hc_origin - Vector( 0.0f, 0.0f, 2.0f );
	TracePlayerBBox( cmd->hc_origin, vecDown, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, pm );
	if ( ( cmd->hc_flags & halfcraft::HC_CMD_ON_GROUND ) && pm.m_pEnt && pm.plane.normal.z >= 0.7f )
		SetGroundEntity( &pm );
	else
		SetGroundEntity( NULL );
	CheckWater();

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

#pragma once

// server.dll: explosions across the two games.
//
// half-life's blasts in minecraft (server_radius_damage, hc_hooks.h): each goes to minecraft as
// proto::kInBlast, which breaks its blocks there the way tnt would. a blast that only echoes one of
// minecraft's own explosions (below) doesn't go back: minecraft broke its blocks itself.
//
// minecraft's explosions in half-life (proto::kEvExplosion, see Combat). half-life's own
// blast hurts its npcs and throws its props; minecraft's stand-ins ignore minecraft's explosions, so
// nothing is hit twice. the blast is blamed on the creeper's stand-in or on nobody, never on the
// player: npcs fight the creeper, and a creeper's blast doesn't count as the player's rocket.
//
// it leaves alone:
//   scripted  what can break and the map's scripting depends on, so a blast can't soft-lock a map: it
//             has a name (the map's i/o, scripted sequences and templates find things by name),
//             outputs wired to something (OnBreak, OnHealthChanged, ...) or a damage filter (only some
//             damage may break it). func_breakables that only break on a trigger never take damage
//             anyway; what can't break at all is pushed as before
//   allies    nobody's blast (tnt) is the player's doing: the npcs half-life keeps safe from the
//             player's own blasts (friendly-damage immune, liked by the player) stay safe
//   stand-ins minecraft's mobs' (hc_mobs): minecraft hurt its own mobs already

#include "core/hc_units.h"

class CBaseEntity;
class CBasePlayer;
class Vector;

namespace halfcraft
{
	/// a map loaded: where its blasts go off in minecraft.
	void half_life_blasts_reset(MapSlot slot);

	/// half-life's blast for a minecraft explosion.
	/// @param centre - source units
	/// @param radius - source units
	/// @param magnitude - half-life's damage at the centre
	/// @param owner - who it's blamed on: a mob's stand-in, or nullptr for nobody (the world)
	/// @param player - minecraft hurt its own player already: the blast leaves them out
	void minecraft_blast(const Vector& centre, float radius, int magnitude, CBaseEntity* owner, CBasePlayer* player);
	/// whether minecraft_blast's half-life blast is going off right now (env_explosion deals it synchronously).
	/// it leaves the player out, but a vehicle hands its share on to its driver: minecraft took that hit already.
	bool minecraft_blast_running();
}

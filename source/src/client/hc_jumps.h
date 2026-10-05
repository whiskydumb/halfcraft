#pragma once

// client.dll: minecraft's own jumps. source refuses a puppet command that moves the player farther
// than any step could (a command made before source teleported the player would drag them back), so
// minecraft says when it moved its player by itself:
//   teleports  minecraft counts the times its server moved its player (proto::McState::teleportCount:
//              ender pearls, chorus fruit, /tp); the commands from then until the jump has landed carry
//              HC_CMD_JUMP, and source takes them wherever the player fits (hc_movement.cpp)
//   fast moves a step minecraft's own speed explains (an elytra dive or a riptide at a low frame rate)
//              carries it too
// a jump source refuses (the player doesn't fit there) resyncs minecraft to source's player.

namespace halfcraft
{
	struct ClientSession;

	/// a puppet command's HC_CMD_JUMP (or 0), and the bookkeeping around it.
	/// @param feet - the command's origin (source units)
	int jump_flags(const ClientSession& session, const float feet[3]);

	/// minecraft doesn't drive the player right now: what it does meanwhile isn't a jump of its own
	/// (the teleport that hands the player back to it is source's).
	void jump_forget(const ClientSession& session);

	/// how far source's player may be from the last puppet command's feet before that's a refusal of
	/// one of minecraft's jumps (0: that command wasn't one).
	float jump_refused_units();
}

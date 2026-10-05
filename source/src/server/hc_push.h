#pragma once

// server.dll: source's pushes on the player while minecraft drives it. a trigger_push, a conveyor
// belt or a point_push moves the player through source's base velocity, which only source's own
// movement reads (CGameMovement::HalfCraftMove takes minecraft's position instead): minecraft gets it
// as proto::kInPush, moves its player along, and keeps it as momentum when it stops.

class CBasePlayer;

namespace halfcraft
{
	class Push
	{
	public:
		/// once per frame.
		/// @param puppeted - minecraft drives the player (otherwise source's own movement pushes it)
		void update(CBasePlayer* player, bool puppeted);

	private:
		int   sent_[3] = {};     // what minecraft was told last (blocks per second * 1000)
		float sent_at_ = -1.0f;  // when (gpGlobals->curtime; -1: never)
		int   held_[3] = {};     // the last push seen, which a short gap keeps going
		float held_at_ = -1.0f;  // when it was seen (gpGlobals->curtime; -1: never)
	};
}

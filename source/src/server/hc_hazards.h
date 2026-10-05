#pragma once

// server.dll: minecraft's fire, lava and magma hurt half-life's npcs the way they'd hurt a mob:
// standing in fire or lava sets them alight (lava burns hard), standing on magma stings. the
// blocks come from client.dll (HalfCraft_HazardAt, read off minecraft's block lights).

#include "core/hc_units.h"

class CBasePlayer;

namespace halfcraft
{
	class Hazards
	{
	public:
		/// once per frame.
		/// @param slot - where the map sits in minecraft (minecraft <-> source placement)
		void update(CBasePlayer* player, MapSlot slot);

	private:
		float next_check_ = 0.0f;
	};
}

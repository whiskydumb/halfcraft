#pragma once

// server.dll: minecraft's fire, lava and magma hurt half-life's npcs the way they'd hurt a mob:
// standing in fire or lava sets them alight (lava burns hard), standing on magma stings. the
// blocks come from client.dll (HalfCraft_HazardAt, read off minecraft's block lights).

class CBasePlayer;

namespace halfcraft
{
	class Hazards
	{
	public:
		/// once per frame.
		/// @param slot - the map's slot (minecraft <-> source placement)
		void update(CBasePlayer* player, int slot);

	private:
		float next_check_ = 0.0f;
	};
}

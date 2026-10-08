#pragma once

// server.dll: half-life's gravity gun tears minecraft's blocks out of its builds. the block the player
// looks at in a halfcraft_blocks wall (only whole cubes without a block entity, softer than obsidian:
// minecraft marks them, proto::kRenSolids) becomes a halfcraft_block_prop, a physics cube the gun holds,
// throws and punts, which client.dll draws with the block's faces (proto::kWeHeldBlock). minecraft takes
// the block out of its world meanwhile (proto::kInTakeBlock). once the cube comes to rest it snaps to the
// block cell its centre is in, and minecraft puts the block back there, like falling sand, or drops it
// as an item without room (proto::kInHeldBlockLanded); the cube goes once the block is there. a cube
// that goes without coming to rest (a level change, dissolved) leaves the block as an item where it was
// (proto::kInHeldBlockLost). cubes aren't saved: a save made while one is held has the block where it
// was taken from (minecraft's checkpoint), and so does loading it.

#include "halfcraft_protocol.h"
#include "core/hc_units.h"

class CBaseEntity;
class Vector;

namespace halfcraft
{
	/// the gravity gun tears the block in `cell` out of `wall` (the halfcraft_blocks entity it's in).
	/// @param cell - minecraft block coords
	/// @param velocity - the cube's at once (a punt), source units a second
	/// @return the cube, or nullptr: a block was taken a moment ago (the gun asks every frame it looks
	///   at the wall), or client.dll isn't there to tell minecraft
	CBaseEntity* take_block(const int cell[3], const Vector& velocity, CBaseEntity* wall);

	class HeldBlocks
	{
	public:
		/// a map loaded: the last one's cubes went with it.
		void reset(MapSlot slot);
		/// once per frame: cubes that came to rest land, and those minecraft is done with go.
		void update();
		/// proto::kEvHeldBlock.
		void on_news(const proto::McEvent& event);
	};
}

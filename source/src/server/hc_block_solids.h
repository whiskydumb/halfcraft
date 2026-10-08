#pragma once

// server.dll: minecraft's blocks as half-life collision. every 16x16x16 section with solid blocks
// becomes an invisible halfcraft_blocks entity: traces (npc movement, bullets, line of sight) call
// into it, and a static physics object stops props. minecraft's own player never sees these (it has
// the real blocks), and they are left out of the collision streamed to minecraft. a bullet that goes
// into a block goes to minecraft too (proto::kInBulletHit), which breaks glass, panes and ice.

#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

#include "core/hc_units.h"
#include "shared/hc_bridge.h"

namespace halfcraft
{
	inline constexpr char BLOCKS_CLASSNAME[] = "halfcraft_blocks";

	class BlockSolids
	{
	public:
		/// a new map: the old entities went with it; everything is asked for again.
		void reset(MapSlot slot);
		/// once per frame: picks up what changed in client.dll and updates the entities.
		void update();

	private:
		void apply(const SolidSection& section);
		/// hc_debug_blocks / hc_debug_drop
		void debug_draw();

		using Key = std::tuple<std::int32_t, std::int32_t, std::int32_t>;

		SolidsSinceFn             solids_since_ = nullptr;
		std::uint32_t             version_ = 0;
		MapSlot                   slot_;
		std::map<Key, EHANDLE>    entities_;
		std::vector<SolidSection> changes_;
		bool                      dropped_ = false;
		EHANDLE                   melon_;
		float                     drop_time_ = 0.0f;
		bool                      melon_early_ = false;
	};
}

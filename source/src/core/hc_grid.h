#pragma once

// the height of minecraft's block grid in a map (MapSlot::grid_z, hc_units.h).
//
// half-life's floors are rarely on a multiple of 40 units. minecraft puts a block placed on a floor
// in cell floor(y + 0.1) (HostRay.placementCell, PLACEMENT_GAP): on a floor r units above a grid line
// the block sinks r units into it, unless r > 36, where it goes on top and floats 40 - r (at most 3).
// r = 36 is a coin flip: y reaches minecraft as a float, a hair under or over k + 0.9 depending on k
// and on how /fp:fast compiled the division, so the block sank 36 or floated 4.
// each map shifts its grid onto its most common floor, so blocks placed there sit flush and every other
// floor keeps that rule. the shift is a function of the map's geometry alone (shared/hc_floors.cpp),
// so it never changes and builds keep their place.
//
// the shift is how far the floor is above the bottom of the cell minecraft put its blocks in while
// the grid was at 0: r when they sank, r - 40 when they floated, so in [-3, 36] and never a whole
// block. those blocks keep their cell and move with the grid by exactly the depth they sank (or the
// gap they floated), which makes them flush too. worked through:
//   d1_trainstation_01's station floor at z = -32 (r = 8):
//     grid at 0:   y = -0.8, cell floor(-0.7) = -1, the block's bottom at -40: sunk 8
//     grid at 8:   y = (-32 - 8) / 40 = -1, cell floor(-0.9) = -1, bottom at -40 + 8 = -32: flush, for
//                  the block placed now and the one placed before (same cell) alike
//   a floor at z = 78 (r = 38):
//     grid at 0:   y = 1.95, cell floor(2.05) = 2, the block's bottom at 80: floats 2
//     grid at -2:  y = 80 / 40 = 2, cell 2, bottom at 80 - 2 = 78: flush
//   a floor at z = 2 (just above a grid line): sunk 2, the grid goes up 2, not down 38.
// at r = 36 the grid goes up 36: a block that floated 4 then floats a whole block, where the other
// way round one that sank 36 would be buried out of sight.

#include <array>
#include <cmath>
#include <map>

namespace halfcraft
{
	inline constexpr int GRID_UNITS = 40;               // UNITS_PER_BLOCK: floors are told apart in whole units
	inline constexpr int PLACEMENT_GAP_UNITS = 4;       // HostRay.PLACEMENT_GAP (0.1 blocks)

	/// how far a floor is above the grid line under it (grid at 0), 0..39.
	constexpr int floor_residue(int floor_z)
	{
		const int r = floor_z % GRID_UNITS;
		return r < 0 ? r + GRID_UNITS : r;
	}

	/// the cell (minecraft y) minecraft places a block in on a floor: HostRay.placementCell, with
	/// r = 36 taken as sinking.
	/// @param above_grid - the floor's height over minecraft's y = 0 (units)
	constexpr int placement_cell(int above_grid)
	{
		const int up = above_grid + PLACEMENT_GAP_UNITS - 1;
		return (up - floor_residue(up)) / GRID_UNITS;
	}

	/// the grid height that puts a floor on a grid line and keeps blocks placed on it with the grid at 0 flush.
	/// @param floor_z - the floor's height (units)
	/// @return units in [-3, 36]
	constexpr int grid_z_for_floor(int floor_z)
	{
		return floor_z - placement_cell(floor_z) * GRID_UNITS;
	}

	/// the bottom (units) of a block minecraft places on a floor, with the grid at grid_z.
	constexpr int placed_block_bottom(int floor_z, int grid_z)
	{
		return placement_cell(floor_z - grid_z) * GRID_UNITS + grid_z;
	}

	/// every floor in [lo, hi) is flush under its own grid height, both for a block placed now and for
	/// one placed while the grid was at 0, and its grid moves less than a block.
	constexpr bool grid_puts_blocks_flush(int lo, int hi)
	{
		for (int floor_z = lo; floor_z < hi; ++floor_z) {
			const int grid_z = grid_z_for_floor(floor_z);
			if (grid_z < -3 || grid_z > 36 || placed_block_bottom(floor_z, grid_z) != floor_z || placed_block_bottom(floor_z, 0) + grid_z != floor_z) {
				return false;
			}
		}
		return true;
	}

	static_assert(grid_z_for_floor(-32) == 8 && placed_block_bottom(-32, 0) == -40);
	static_assert(grid_z_for_floor(78) == -2 && placed_block_bottom(78, 0) == 80);
	static_assert(grid_z_for_floor(76) == 36 && placed_block_bottom(76, 0) == 40 && grid_z_for_floor(77) == -3);
	static_assert(grid_z_for_floor(256) == 16 && grid_z_for_floor(80) == 0 && grid_z_for_floor(2) == 2 && grid_z_for_floor(-2) == -2);
	static_assert(grid_puts_blocks_flush(-2000, 2000));

	/// floor heights weighed against each other. the grid goes onto the most weighed residue (floors
	/// 40 units apart share one grid), and onto its most weighed height for the log.
	class FloorVotes
	{
	public:
		struct Pick
		{
			int    floor_z;  // the most weighed floor of the winning residue
			int    grid_z;   // grid_z_for_floor(floor_z)
			double share;    // of all the weight, the winning residue's (0..1)
		};

		void add(float floor_z, double weight) { weights_[static_cast<int>(std::lround(floor_z))] += weight; }

		bool empty() const { return weights_.empty(); }

		/// ties go to the lower residue and the lower floor, so it never depends on the order of votes.
		/// @note: don't change how it picks: that moves existing builds (see hc_floors.cpp).
		Pick pick() const
		{
			std::array<double, GRID_UNITS> residues{};
			double                         total = 0.0;
			for (const auto& entry : weights_) {
				residues[floor_residue(entry.first)] += entry.second;
				total += entry.second;
			}
			int residue = 0;
			for (int r = 1; r < GRID_UNITS; ++r) {
				if (residues[r] > residues[residue]) {
					residue = r;
				}
			}
			int    floor_z = residue;
			double best = -1.0;
			for (const auto& entry : weights_) {
				if (floor_residue(entry.first) == residue && entry.second > best) {
					floor_z = entry.first;
					best = entry.second;
				}
			}
			return { floor_z, grid_z_for_floor(floor_z), total > 0.0 ? residues[residue] / total : 0.0 };
		}

	private:
		std::map<int, double> weights_;  // whole units -> weight
	};
}

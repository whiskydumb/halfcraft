#pragma once

#include <cstdint>
#include <string>

// source <-> minecraft space.
//
// source is z-up (x east, y north); minecraft is y-up (x east, z south). the scale makes minecraft's
// player exactly source's: 1.8 blocks = 72 units tall (source's standing hull), eyes at 1.62 blocks =
// 64.8 units (source's eye height is 64).
//
// every map gets its own stretch of the minecraft world (MAP_SLOT_BLOCKS apart along x): source maps
// all sit around their own origin, and builds from one map must not show up in the next. a source
// map spans at most +-16384 units (+-410 blocks), so a slot of 1024 blocks always fits one. the block
// grid's height differs per map too: it's lifted onto the map's most common floor (core/hc_grid.h).

namespace halfcraft
{
	inline constexpr double UNITS_PER_BLOCK = 40.0;
	inline constexpr double MAP_SLOT_BLOCKS = 1024.0;

	struct McVec
	{
		double x, y, z;
	};

	/// where a map sits in the minecraft world.
	struct MapSlot
	{
		int   index = 0;      // its stretch along x (see map_slot)
		float grid_z = 0.0f;  // source z of minecraft's y = 0: the block grid's height in this map (see map_grid_z)

		/// minecraft x of the map's source origin
		double x_blocks() const { return index * MAP_SLOT_BLOCKS; }

		bool operator==(const MapSlot& other) const { return index == other.index && grid_z == other.grid_z; }
	};

	/// @param p - source position (units)
	/// @param slot - where the map sits in minecraft
	/// @return the same point in minecraft blocks
	inline McVec source_to_mc(const float p[3], MapSlot slot)
	{
		return { p[0] / UNITS_PER_BLOCK + slot.x_blocks(), (p[2] - slot.grid_z) / UNITS_PER_BLOCK, -p[1] / UNITS_PER_BLOCK };
	}

	/// @param out - the same point in source units
	inline void mc_to_source(double x, double y, double z, MapSlot slot, float out[3])
	{
		out[0] = static_cast<float>((x - slot.x_blocks()) * UNITS_PER_BLOCK);
		out[1] = static_cast<float>(-z * UNITS_PER_BLOCK);
		out[2] = static_cast<float>(y * UNITS_PER_BLOCK + slot.grid_z);
	}

	/// a direction (no scale, no slot) from source axes to minecraft axes.
	inline void source_dir_to_mc(const float d[3], float out[3])
	{
		out[0] = d[0];
		out[1] = d[2];
		out[2] = -d[1];
	}

	inline float wrap_degrees(float degrees)
	{
		while (degrees > 180.0f) {
			degrees -= 360.0f;
		}
		while (degrees <= -180.0f) {
			degrees += 360.0f;
		}
		return degrees;
	}

	// source yaw: 0 = +x, counter-clockwise from above. minecraft yaw: 0 = +z (south), clockwise.
	// the mapping is its own inverse. pitch is positive looking down in both games.
	inline float source_yaw_to_mc(float yaw) { return wrap_degrees(-yaw - 90.0f); }
	inline float mc_yaw_to_source(float yaw) { return wrap_degrees(-yaw - 90.0f); }

	/// minecraft's fov is vertical; source's view fov is horizontal at 4:3 (the engine widens it for
	/// the real aspect ratio afterwards).
	float mc_fov_to_source(float vertical_degrees);

	/// where the map comes in the campaign's play order (half-life 2, then the episodes).
	/// @return -1 for a map that isn't one of the campaign's
	int campaign_order(const char* map_name);

	/// stable id for a map (minecraft drops its collision when this changes). never 0.
	std::uint32_t map_world_id(const char* map_name);
	/// the map's slot in the minecraft world: the campaign's maps in story order, anything else
	/// hashed into the slots after them.
	int map_slot(const char* map_name);
	/// "maps/d1_trainstation_01.bsp" or "D1_TrainStation_01" -> "d1_trainstation_01"
	std::string map_base_name(const char* map_name);
}

#pragma once

// server.dll: what half-life 2's player collides with, as minecraft geometry. include after the sdk
// headers and tier0/valve_minmax_off.h.
//
//   world brushes      enginetrace brush planes        -> convexes (exact, player clips included; the
//                                                         sky's flagged PRIM_SKY, from the map's .bsp)
//   displacements      enginetrace displacement collide -> triangles
//   static props       their .phy hulls                -> convexes
//   solid entities     doors, lifts, props, crates     -> convexes, re-sent while they move
//
// npcs and players are left out: npcs become minecraft actors, not walls.

#include <unordered_map>
#include <vector>

#include "core/hc_collision.h"

class CBaseEntity;
struct model_t;

namespace halfcraft
{
	class WorldCollision final : public CollisionSource
	{
	public:
		/// a new map: where it sits in the minecraft world, and nothing cached from the last one.
		/// @param map_name - its name ("d1_canals_01"), to read its sky brushes from; nullptr: none
		void reset(MapSlot slot, const char* map_name);

		void gather(const float lo[3], const float hi[3], ColPrimitives& out) override;

		/// hc_debug_voxels: voxelizes the regions around a point again and checks each voxel against
		/// source's own collision (box traces, also from above and below: a displacement is one-sided),
		/// logging the voxels nothing in source is near.
		/// @param centre - minecraft coords
		/// @param radius - regions around it horizontally (one above and below)
		void check_voxels(const McVec& centre, int radius);

		/// solid entities near the player that moved since the last frame: the regions they left
		/// and entered go to minecraft again.
		/// @param player - the player's feet, minecraft coords
		void track_movers(const McVec& player, CollisionStreamer& streamer);

	private:
		using Hull = std::vector<Vector>;  // a convex piece as triangles (3 corners each), model space

		const std::vector<Hull>& model_hulls(const model_t* model);
		void                     add_hulls(const std::vector<Hull>& hulls, const matrix3x4_t& to_world, std::uint32_t flags, ColPrimitives& out) const;
		void                     add_box(const Vector& mins, const Vector& maxs, const matrix3x4_t& to_world, ColPrimitives& out) const;
		void                     add_brushes(const Vector& mins, const Vector& maxs, ColPrimitives& out) const;
		void                     add_displacements(const Vector& mins, const Vector& maxs, ColPrimitives& out) const;
		void                     add_static_props(const Vector& mins, const Vector& maxs, ColPrimitives& out);
		void                     add_entities(const Vector& mins, const Vector& maxs, ColPrimitives& out);
		void                     add_entity(CBaseEntity* entity, ColPrimitives& out);

		/// minecraft box -> source box
		void to_source_box(const float lo[3], const float hi[3], Vector& mins, Vector& maxs) const;
		/// source box -> minecraft box
		void to_mc_box(const Vector& mins, const Vector& maxs, float lo[3], float hi[3]) const;

		struct Mover
		{
			Vector origin;
			QAngle angles;
			Vector mins, maxs;  // world bounds when last seen
			int    frame;
		};

		MapSlot                                               slot_;
		std::vector<bool>                                     sky_brushes_;  // by brush index
		std::unordered_map<const model_t*, std::vector<Hull>> hulls_;
		std::unordered_map<int, Mover>                        movers_;  // by entity serial-qualified index
		int                                                   frame_ = 0;
	};
}

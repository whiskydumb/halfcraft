#pragma once

// server.dll: what half-life 2's player collides with, as minecraft geometry. include after the sdk
// headers and tier0/valve_minmax_off.h.
//
//   world brushes      enginetrace brush planes        -> convexes (exact, player clips included)
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
		/// a new map: its slot in the minecraft world, and nothing cached from the last one.
		void reset(int slot);

		void gather(const float lo[3], const float hi[3], ColPrimitives& out) override;

		/// solid entities near the player that moved since the last frame: the regions they left
		/// and entered go to minecraft again.
		/// @param player - the player's feet, minecraft coords
		void track_movers(const McVec& player, CollisionStreamer& streamer);

	private:
		using Hull = std::vector<Vector>;  // a convex piece as triangles (3 corners each), model space

		const std::vector<Hull>& model_hulls(const model_t* model);
		void add_hulls(const std::vector<Hull>& hulls, const matrix3x4_t& to_world, std::uint32_t flags, ColPrimitives& out) const;
		void add_box(const Vector& mins, const Vector& maxs, const matrix3x4_t& to_world, ColPrimitives& out) const;
		void add_brushes(const Vector& mins, const Vector& maxs, ColPrimitives& out) const;
		void add_displacements(const Vector& mins, const Vector& maxs, ColPrimitives& out) const;
		void add_static_props(const Vector& mins, const Vector& maxs, ColPrimitives& out);
		void add_entities(const Vector& mins, const Vector& maxs, ColPrimitives& out);
		void add_entity(CBaseEntity* entity, ColPrimitives& out);

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

		int                                                 slot_ = 0;
		std::unordered_map<const model_t*, std::vector<Hull>> hulls_;
		std::unordered_map<int, Mover>                      movers_;  // by entity serial-qualified index
		int                                                 frame_ = 0;
	};
}

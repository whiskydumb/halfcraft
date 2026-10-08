#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "hc_link.h"
#include "hc_units.h"
#include "hc_collision_shapes.h"

// streams the game's collision around the player to minecraft, in 8x8x8-block regions: the exact
// triangles (minecraft's smooth player collider) and an 8x8x8 sub-voxel mask per block (everything
// else minecraft asks about: block placement, mobs, items). ported from SkyCraft's Collision.cpp.
//
// main thread: picks the regions that need (re)sending and asks the CollisionSource what is in them.
// worker thread: triangulates, voxelizes and writes the collision ring.

namespace halfcraft
{
	/// what the player collides with, supplied by the game (server.dll).
	class CollisionSource
	{
	public:
		virtual ~CollisionSource() = default;
		/// adds every solid thing overlapping the minecraft box [lo, hi]. main thread.
		virtual void gather(const float lo[3], const float hi[3], ColPrimitives& out) = 0;
	};

	class CollisionStreamer
	{
	public:
		static constexpr int REGION_SIZE = COLLISION_REGION_SIZE;  // blocks per region edge (must match the java side)
		static constexpr int RADIUS = 8;  // regions streamed around the player horizontally: 64+ blocks, as far as npc stand-ins (hc_combat)

		explicit CollisionStreamer(Link& link);
		~CollisionStreamer();
		CollisionStreamer(const CollisionStreamer&) = delete;
		CollisionStreamer& operator=(const CollisionStreamer&) = delete;

		/// drops everything; minecraft clears its store when it sees the new epoch.
		void reset(std::uint32_t epoch);
		/// once per frame, main thread.
		/// @param player - the player's feet in minecraft coords
		void update(const McVec& player, CollisionSource& source);
		/// something moved: regions overlapping the minecraft box [lo, hi] go out again first.
		void invalidate(const float lo[3], const float hi[3]);
		/// minecraft wants the collision under a point (a projectile falling out of the bottom of the
		/// volume around the player waits there): the column of regions below it goes out too, however
		/// far from the player.
		/// @param point - minecraft coords
		void want_below(const McVec& point);

	private:
		struct Job
		{
			int           rx = 0, ry = 0, rz = 0;
			std::uint32_t epoch = 0;
			bool          clear = false;
			ColPrimitives prims;
		};

		void harvest(int rx, int ry, int rz, CollisionSource& source);
		void worker_loop();
		void send(const std::vector<std::uint8_t>& payload, proto::ColType type);
		void send_triangles(const Job& job, const std::vector<ColPrimitives::Tri>& solid);
		void voxelize(const Job& job);

		using Clock = std::chrono::steady_clock;

		Link&                                                link_;
		std::mutex                                           mutex_;
		std::condition_variable                              cv_;
		std::deque<Job>                                      queue_;
		std::thread                                          worker_;
		std::atomic<bool>                                    stop_{ false };
		std::atomic<std::uint32_t>                           epoch_{ 0 };
		std::unordered_map<std::uint64_t, Clock::time_point> harvested_;
		std::vector<std::array<int, 3>>                      urgent_;
		std::vector<std::array<int, 3>>                      wanted_;  // want_below's, sent once each
		std::vector<std::array<int, 3>>                      offsets_;
		// regions new to the stream since it last had them all (a teleport, a map load): logged once
		// they're all out, with what gathering them cost the main thread
		Clock::time_point settle_start_{};
		Clock::duration   settle_cost_{};
		std::size_t       settle_regions_ = 0;
	};
}

#include "hc_collision.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>

#include "hc_log.h"

namespace halfcraft
{
	namespace
	{
		using namespace std::chrono_literals;

		constexpr int   BELOW = 3;   // regions below the player
		constexpr int   ABOVE = 2;   // regions above the player
		constexpr int   GRID = REGION_VOXELS;  // voxels per region edge (64)
		constexpr auto  REFRESH_NEAR = 1000ms;   // re-send regions next to the player this often
		constexpr auto  FRAME_BUDGET = 2500us;
		constexpr int   MAX_HARVESTS_PER_FRAME = 3;

		bool finite(const float* v, int n)
		{
			for (int i = 0; i < n; ++i) {
				if (!std::isfinite(v[i]) || std::fabs(v[i]) > 1.0e7f) {
					return false;
				}
			}
			return true;
		}

		bool overlaps(const float* a_lo, const float* a_hi, const float* b_lo, const float* b_hi)
		{
			return a_lo[0] <= b_hi[0] && a_hi[0] >= b_lo[0] && a_lo[1] <= b_hi[1] && a_hi[1] >= b_lo[1] && a_lo[2] <= b_hi[2] && a_hi[2] >= b_lo[2];
		}

		std::uint64_t region_key(int x, int y, int z)
		{
			return (std::uint64_t(std::uint32_t(x) & 0x1FFFFF) << 42) | (std::uint64_t(std::uint32_t(y) & 0x1FFFFF) << 21) | (std::uint32_t(z) & 0x1FFFFF);
		}

		int floor_div(int v, int d)
		{
			return v >= 0 ? v / d : -((-v + d - 1) / d);
		}
	}

	CollisionStreamer::CollisionStreamer(Link& link) : link_(link)
	{
		for (int dx = -RADIUS; dx <= RADIUS; ++dx) {
			for (int dz = -RADIUS; dz <= RADIUS; ++dz) {
				for (int dy = -BELOW; dy <= ABOVE; ++dy) {
					offsets_.push_back({ dx, dy, dz });
				}
			}
		}
		std::sort(offsets_.begin(), offsets_.end(), [](const auto& a, const auto& b) {
			return a[0] * a[0] + a[2] * a[2] + a[1] * a[1] * 2 < b[0] * b[0] + b[2] * b[2] + b[1] * b[1] * 2;
		});
		worker_ = std::thread([this] { worker_loop(); });
	}

	CollisionStreamer::~CollisionStreamer()
	{
		{
			std::lock_guard<std::mutex> lock(mutex_);
			stop_ = true;
			queue_.clear();
		}
		cv_.notify_all();
		if (worker_.joinable()) {
			worker_.join();
		}
	}

	void CollisionStreamer::reset(std::uint32_t epoch)
	{
		epoch_ = epoch;
		harvested_.clear();
		urgent_.clear();
		settle_regions_ = 0;
		std::lock_guard<std::mutex> lock(mutex_);
		queue_.clear();
		Job job;
		job.clear = true;
		job.epoch = epoch;
		queue_.push_back(std::move(job));
		cv_.notify_one();
	}

	void CollisionStreamer::invalidate(const float lo[3], const float hi[3])
	{
		// triangles are sent with every region they come within half a block of.
		const int x0 = floor_div(static_cast<int>(std::floor(lo[0] - 1.0f)), REGION_SIZE), x1 = floor_div(static_cast<int>(std::floor(hi[0] + 1.0f)), REGION_SIZE);
		const int y0 = floor_div(static_cast<int>(std::floor(lo[1] - 1.0f)), REGION_SIZE), y1 = floor_div(static_cast<int>(std::floor(hi[1] + 1.0f)), REGION_SIZE);
		const int z0 = floor_div(static_cast<int>(std::floor(lo[2] - 1.0f)), REGION_SIZE), z1 = floor_div(static_cast<int>(std::floor(hi[2] + 1.0f)), REGION_SIZE);
		if ((x1 - x0 + 1) * (y1 - y0 + 1) * (z1 - z0 + 1) > 64) {
			return;  // something huge moved (a whole train): the periodic refresh picks it up
		}
		for (int rx = x0; rx <= x1; ++rx) {
			for (int ry = y0; ry <= y1; ++ry) {
				for (int rz = z0; rz <= z1; ++rz) {
					const std::array<int, 3> r{ rx, ry, rz };
					if (std::find(urgent_.begin(), urgent_.end(), r) == urgent_.end()) {
						urgent_.push_back(r);
					}
				}
			}
		}
	}

	void CollisionStreamer::update(const McVec& player, CollisionSource& source)
	{
		const int  prx = static_cast<int>(std::floor(player.x / REGION_SIZE));
		const int  pry = static_cast<int>(std::floor(player.y / REGION_SIZE));
		const int  prz = static_cast<int>(std::floor(player.z / REGION_SIZE));
		const auto start = Clock::now();
		int        done = 0;

		// regions something just moved through: their collision changed.
		while (!urgent_.empty() && done < MAX_HARVESTS_PER_FRAME * 2) {
			const auto r = urgent_.back();
			urgent_.pop_back();
			if (std::abs(r[0] - prx) > RADIUS + 1 || std::abs(r[2] - prz) > RADIUS + 1 || r[1] - pry < -BELOW - 1 || r[1] - pry > ABOVE + 1) {
				harvested_.erase(region_key(r[0], r[1], r[2]));  // far away: sent again whenever it's needed
				continue;
			}
			harvest(r[0], r[1], r[2], source);
			harvested_[region_key(r[0], r[1], r[2])] = start;
			++done;
		}
		bool complete = true;  // every region around the player has gone out
		for (const auto& o : offsets_) {
			const int  rx = prx + o[0], ry = pry + o[1], rz = prz + o[2];
			const auto key = region_key(rx, ry, rz);
			const auto it = harvested_.find(key);
			const bool is_near = std::abs(o[0]) <= 1 && std::abs(o[2]) <= 1 && o[1] >= -1 && o[1] <= 0;
			if (it != harvested_.end() && !(is_near && start - it->second > REFRESH_NEAR)) {
				continue;
			}
			const bool fresh = it == harvested_.end();
			const auto began = Clock::now();
			harvest(rx, ry, rz, source);
			harvested_[key] = start;
			if (fresh) {
				if (settle_regions_++ == 0) {
					settle_start_ = start;
					settle_cost_ = {};
				}
				settle_cost_ += Clock::now() - began;
			}
			if (++done >= MAX_HARVESTS_PER_FRAME || Clock::now() - start > FRAME_BUDGET) {
				complete = false;
				break;
			}
		}
		if (complete && settle_regions_ > 0) {
			// only the big ones (a teleport, a map load): walking on streams a strip at a time
			if (settle_regions_ * 2 >= offsets_.size()) {
				std::size_t queued = 0;
				{
					std::lock_guard<std::mutex> lock(mutex_);
					queued = queue_.size();
				}
				const double seconds = std::chrono::duration<double>(start - settle_start_).count();
				const double cost_ms = std::chrono::duration<double, std::milli>(settle_cost_).count();
				log_info("collision: streamed %zu regions around the player in %.1f s (%.1f ms of the main thread, %.2f ms a region; %zu still queued for the worker)",
					settle_regions_, seconds, cost_ms, cost_ms / static_cast<double>(settle_regions_), queued);
			}
			settle_regions_ = 0;
		}

		// bound memory: drop bookkeeping for far-away regions.
		if (harvested_.size() > offsets_.size() * 4) {
			harvested_.clear();
		}
	}

	void CollisionStreamer::harvest(int rx, int ry, int rz, CollisionSource& source)
	{
		Job job;
		job.rx = rx;
		job.ry = ry;
		job.rz = rz;
		job.epoch = epoch_.load();
		// half a block of slack: triangles near the border are sent with both regions.
		const float lo[3] = { float(rx * REGION_SIZE) - 0.5f, float(ry * REGION_SIZE) - 0.5f, float(rz * REGION_SIZE) - 0.5f };
		const float hi[3] = { lo[0] + REGION_SIZE + 1.0f, lo[1] + REGION_SIZE + 1.0f, lo[2] + REGION_SIZE + 1.0f };
		source.gather(lo, hi, job.prims);
		std::lock_guard<std::mutex> lock(mutex_);
		queue_.push_back(std::move(job));
		cv_.notify_one();
	}

	void CollisionStreamer::worker_loop()
	{
		std::vector<ColPrimitives::Tri> solid;
		while (true) {
			Job job;
			{
				std::unique_lock<std::mutex> lock(mutex_);
				cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
				if (stop_) {
					return;
				}
				job = std::move(queue_.front());
				queue_.pop_front();
			}
			if (job.clear) {
				std::vector<std::uint8_t> payload(4);
				std::memcpy(payload.data(), &job.epoch, 4);
				send(payload, proto::kColClear);
			} else if (job.epoch == epoch_.load()) {
				solid.clear();
				triangulate(job.prims, solid);
				send_triangles(job, solid);
				voxelize(job);
			}
		}
	}

	void CollisionStreamer::send(const std::vector<std::uint8_t>& payload, proto::ColType type)
	{
		for (int attempt = 0; attempt < 2000 && !stop_; ++attempt) {
			if (link_.write_collision(type, payload.data(), static_cast<std::uint32_t>(payload.size()))) {
				return;
			}
			std::this_thread::sleep_for(1ms);  // ring full: minecraft is behind (or not running)
		}
		if (!stop_) {
			log_warning("collision ring stayed full; dropped a message");
		}
	}

	void CollisionStreamer::send_triangles(const Job& job, const std::vector<ColPrimitives::Tri>& solid)
	{
		const float lo[3] = { float(job.rx * REGION_SIZE) - 0.5f, float(job.ry * REGION_SIZE) - 0.5f, float(job.rz * REGION_SIZE) - 0.5f };
		const float hi[3] = { lo[0] + REGION_SIZE + 1.0f, lo[1] + REGION_SIZE + 1.0f, lo[2] + REGION_SIZE + 1.0f };
		std::vector<proto::ColTri> out;
		out.reserve(solid.size());
		for (const auto& tri : solid) {
			float tlo[3], thi[3];
			for (int k = 0; k < 3; ++k) {
				tlo[k] = std::min({ tri.v[k], tri.v[3 + k], tri.v[6 + k] });
				thi[k] = std::max({ tri.v[k], tri.v[3 + k], tri.v[6 + k] });
			}
			if (overlaps(tlo, thi, lo, hi) && finite(tri.v, 9)) {
				proto::ColTri t{};
				std::memcpy(t.v, tri.v, sizeof(t.v));
				t.flags = tri.flags;
				out.push_back(t);
			}
		}
		proto::ColRegion header{};
		header.minX = job.rx * REGION_SIZE;
		header.minY = job.ry * REGION_SIZE;
		header.minZ = job.rz * REGION_SIZE;
		header.maxX = header.minX + REGION_SIZE - 1;
		header.maxY = header.minY + REGION_SIZE - 1;
		header.maxZ = header.minZ + REGION_SIZE - 1;
		header.epoch = job.epoch;
		header.count = static_cast<std::uint32_t>(out.size());
		std::vector<std::uint8_t> payload(sizeof(header) + out.size() * sizeof(proto::ColTri));
		std::memcpy(payload.data(), &header, sizeof(header));
		if (!out.empty()) {
			std::memcpy(payload.data() + sizeof(header), out.data(), out.size() * sizeof(proto::ColTri));
		}
		send(payload, proto::kColTris);
	}

	static_assert(PRIM_SKY == proto::kTriSky);

	void CollisionStreamer::voxelize(const Job& job)
	{
		constexpr int G = GRID;
		RegionVoxels  solid, sky;
		voxelize_region(job.prims, job.rx, job.ry, job.rz, solid, &sky);

		// pack the non-empty blocks.
		std::vector<proto::ColBlock> blocks;
		blocks.reserve(64);
		for (int by = 0; by < REGION_SIZE; ++by) {
			for (int bz = 0; bz < REGION_SIZE; ++bz) {
				for (int bx = 0; bx < REGION_SIZE; ++bx) {
					proto::ColBlock blk{};
					bool            any = false;
					bool            all_sky = true;  // every voxel of it is the sky's
					for (int sy = 0; sy < 8; ++sy) {
						std::uint64_t layer = 0, sky_layer = 0;
						for (int sz = 0; sz < 8; ++sz) {
							const int  row_index = (by * 8 + sy) * G + (bz * 8 + sz);
							const auto row = (solid[row_index] >> (bx * 8)) & 0xFF;
							layer |= row << (sz * 8);
							sky_layer |= ((sky[row_index] >> (bx * 8)) & 0xFF) << (sz * 8);
						}
						blk.bits[sy] = layer;
						any |= layer != 0;
						all_sky &= (layer & ~sky_layer) == 0;
					}
					if (any) {
						blk.flags = all_sky ? proto::kColBlockSky : 0u;
						blk.x = job.rx * REGION_SIZE + bx;
						blk.y = job.ry * REGION_SIZE + by;
						blk.z = job.rz * REGION_SIZE + bz;
						blocks.push_back(blk);
					}
				}
			}
		}

		proto::ColRegion header{};
		header.minX = job.rx * REGION_SIZE;
		header.minY = job.ry * REGION_SIZE;
		header.minZ = job.rz * REGION_SIZE;
		header.maxX = header.minX + REGION_SIZE - 1;
		header.maxY = header.minY + REGION_SIZE - 1;
		header.maxZ = header.minZ + REGION_SIZE - 1;
		header.epoch = job.epoch;
		header.count = static_cast<std::uint32_t>(blocks.size());
		std::vector<std::uint8_t> payload(sizeof(header) + blocks.size() * sizeof(proto::ColBlock));
		std::memcpy(payload.data(), &header, sizeof(header));
		if (!blocks.empty()) {
			std::memcpy(payload.data() + sizeof(header), blocks.data(), blocks.size() * sizeof(proto::ColBlock));
		}
		send(payload, proto::kColRegion);
	}
}

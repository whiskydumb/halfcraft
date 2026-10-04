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

		constexpr int   RADIUS = 5;  // regions around the player horizontally (arrows fly far)
		constexpr int   BELOW = 3;   // regions below the player
		constexpr int   ABOVE = 2;   // regions above the player
		constexpr int   GRID = CollisionStreamer::REGION_SIZE * 8;  // voxels per region edge (64)
		constexpr auto  REFRESH_NEAR = 1000ms;   // re-send regions next to the player this often
		constexpr auto  FRAME_BUDGET = 2500us;
		constexpr int   MAX_HARVESTS_PER_FRAME = 3;
		constexpr float STEEP_MIN = 0.1f;    // |n.y| below this is a wall: keep it fine-grained
		constexpr float STEEP_MAX = 0.643f;  // |n.y| below this (steeper than ~50 deg) gets block-coarsened
		constexpr float PRIM_MARGIN = 0.5f;  // voxels; lets thin convex shapes still register

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

		// ---- triangle / box overlap (akenine-moller SAT), voxel units ------------------------------
		inline void sub(const float* a, const float* b, float* o) { o[0] = a[0] - b[0], o[1] = a[1] - b[1], o[2] = a[2] - b[2]; }
		inline void cross(const float* a, const float* b, float* o)
		{
			o[0] = a[1] * b[2] - a[2] * b[1];
			o[1] = a[2] * b[0] - a[0] * b[2];
			o[2] = a[0] * b[1] - a[1] * b[0];
		}
		inline float dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

		bool axis_test(const float* v0, const float* v1, const float* v2, const float* axis, float h)
		{
			const float p0 = dot(v0, axis), p1 = dot(v1, axis), p2 = dot(v2, axis);
			const float mn = std::min({ p0, p1, p2 }), mx = std::max({ p0, p1, p2 });
			const float r = h * (std::fabs(axis[0]) + std::fabs(axis[1]) + std::fabs(axis[2]));
			return !(mn > r || mx < -r);
		}

		// box centred at c with half-size h (all axes). triangle corners ta/tb/tc, face normal n.
		bool tri_box_overlap(const float* c, float h, const float* ta, const float* tb, const float* tc, const float* n)
		{
			float v0[3], v1[3], v2[3];
			sub(ta, c, v0);
			sub(tb, c, v1);
			sub(tc, c, v2);
			for (int i = 0; i < 3; ++i) {
				const float mn = std::min({ v0[i], v1[i], v2[i] }), mx = std::max({ v0[i], v1[i], v2[i] });
				if (mn > h || mx < -h) {
					return false;
				}
			}
			const float d = dot(n, v0);
			const float r = h * (std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]));
			if (std::fabs(d) > r) {
				return false;
			}
			float e[3][3];
			sub(v1, v0, e[0]);
			sub(v2, v1, e[1]);
			sub(v0, v2, e[2]);
			static constexpr float AXES[3][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
			for (auto& edge : e) {
				for (auto& unit : AXES) {
					float axis[3];
					cross(edge, unit, axis);
					if (!axis_test(v0, v1, v2, axis, h)) {
						return false;
					}
				}
			}
			return true;
		}

		// convex hulls (planes) -> outward-wound triangles: clip a big square on each plane by all the
		// others. appended to out after the source's own triangles.
		void triangulate(const ColPrimitives& src, std::vector<ColPrimitives::Tri>& out)
		{
			out.insert(out.end(), src.tris.begin(), src.tris.end());
			std::vector<std::array<float, 3>> poly, clipped;
			for (const auto& cvx : src.convexes) {
				const float ex = cvx.hi[0] - cvx.lo[0], ey = cvx.hi[1] - cvx.lo[1], ez = cvx.hi[2] - cvx.lo[2];
				const float diag = std::sqrt(ex * ex + ey * ey + ez * ez) + 1.0f;
				const float mid[3] = { (cvx.lo[0] + cvx.hi[0]) * 0.5f, (cvx.lo[1] + cvx.hi[1]) * 0.5f, (cvx.lo[2] + cvx.hi[2]) * 0.5f };
				for (std::size_t i = 0; i < cvx.planes.size(); ++i) {
					const auto& pl = cvx.planes[i];
					const float n[3] = { pl[0], pl[1], pl[2] };
					const float nl = std::sqrt(dot(n, n));
					if (nl < 1e-6f) {
						continue;
					}
					const float dist = (dot(n, mid) + pl[3]) / (nl * nl);
					const float o[3] = { mid[0] - n[0] * dist, mid[1] - n[1] * dist, mid[2] - n[2] * dist };
					const float ref[3] = { std::fabs(n[1]) < 0.9f * nl ? 0.0f : 1.0f, std::fabs(n[1]) < 0.9f * nl ? 1.0f : 0.0f, 0.0f };
					float       t1[3], t2[3];
					cross(ref, n, t1);
					const float lt = std::sqrt(dot(t1, t1));
					for (float& k : t1) {
						k /= lt;
					}
					cross(n, t1, t2);
					const float l2 = std::sqrt(dot(t2, t2));
					for (float& k : t2) {
						k /= l2;
					}
					poly.clear();
					const float sgn1[4] = { -1, 1, 1, -1 }, sgn2[4] = { -1, -1, 1, 1 };
					for (int q = 0; q < 4; ++q) {
						const float s1 = sgn1[q] * diag, s2 = sgn2[q] * diag;
						poly.push_back({ o[0] + t1[0] * s1 + t2[0] * s2, o[1] + t1[1] * s1 + t2[1] * s2, o[2] + t1[2] * s1 + t2[2] * s2 });
					}
					for (std::size_t j = 0; j < cvx.planes.size() && poly.size() >= 3; ++j) {
						if (j == i) {
							continue;
						}
						const auto& cp = cvx.planes[j];
						clipped.clear();
						for (std::size_t v = 0; v < poly.size(); ++v) {
							const auto& a = poly[v];
							const auto& b = poly[(v + 1) % poly.size()];
							const float da = cp[0] * a[0] + cp[1] * a[1] + cp[2] * a[2] + cp[3];
							const float db = cp[0] * b[0] + cp[1] * b[1] + cp[2] * b[2] + cp[3];
							if (da <= 0.0f) {
								clipped.push_back(a);
							}
							if ((da <= 0.0f) != (db <= 0.0f)) {
								const float t = da / (da - db);
								clipped.push_back({ a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t });
							}
						}
						poly.swap(clipped);
					}
					for (std::size_t v = 1; v + 1 < poly.size(); ++v) {
						ColPrimitives::Tri t{ { poly[0][0], poly[0][1], poly[0][2], poly[v][0], poly[v][1], poly[v][2], poly[v + 1][0], poly[v + 1][1], poly[v + 1][2] }, cvx.flags };
						float e1[3], e2[3], tn[3];
						sub(t.v + 3, t.v, e1);
						sub(t.v + 6, t.v, e2);
						cross(e1, e2, tn);
						if (dot(tn, n) < 0.0f) {
							std::swap_ranges(t.v + 3, t.v + 6, t.v + 6);
						}
						out.push_back(t);
					}
				}
			}
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
		for (const auto& o : offsets_) {
			const int  rx = prx + o[0], ry = pry + o[1], rz = prz + o[2];
			const auto key = region_key(rx, ry, rz);
			const auto it = harvested_.find(key);
			const bool is_near = std::abs(o[0]) <= 1 && std::abs(o[2]) <= 1 && o[1] >= -1 && o[1] <= 0;
			if (it != harvested_.end() && !(is_near && start - it->second > REFRESH_NEAR)) {
				continue;
			}
			harvest(rx, ry, rz, source);
			harvested_[key] = start;
			if (++done >= MAX_HARVESTS_PER_FRAME || Clock::now() - start > FRAME_BUDGET) {
				break;
			}
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

	void CollisionStreamer::voxelize(const Job& job)
	{
		constexpr int              G = GRID;
		std::vector<std::uint64_t> solid(G * G, 0), steep(G * G, 0);
		auto set = [&](std::vector<std::uint64_t>& grid, int x, int y, int z) { grid[y * G + z] |= 1ull << x; };

		const float ox = float(job.rx * REGION_SIZE), oy = float(job.ry * REGION_SIZE), oz = float(job.rz * REGION_SIZE);
		auto to_voxel = [&](const float* mc, float* out) {
			out[0] = (mc[0] - ox) * 8.0f;
			out[1] = (mc[1] - oy) * 8.0f;
			out[2] = (mc[2] - oz) * 8.0f;
		};
		auto clamp_lo = [&](float v) { return std::clamp(static_cast<int>(std::floor(v)), 0, G - 1); };
		auto clamp_hi = [&](float v) { return std::clamp(static_cast<int>(std::ceil(v)) - 1, 0, G - 1); };

		// triangles: plane-guided SAT test so big triangles cost O(area) instead of O(volume).
		for (const auto& tri : job.prims.tris) {
			float a[3], b[3], c[3];
			to_voxel(tri.v, a);
			to_voxel(tri.v + 3, b);
			to_voxel(tri.v + 6, c);
			float lo[3], hi[3];
			for (int i = 0; i < 3; ++i) {
				lo[i] = std::min({ a[i], b[i], c[i] });
				hi[i] = std::max({ a[i], b[i], c[i] });
			}
			if (hi[0] < 0 || hi[1] < 0 || hi[2] < 0 || lo[0] > G || lo[1] > G || lo[2] > G) {
				continue;
			}
			float e1[3], e2[3], n[3];
			sub(b, a, e1);
			sub(c, a, e2);
			cross(e1, e2, n);
			const float len = std::sqrt(dot(n, n));
			if (len < 1e-9f) {
				continue;
			}
			n[0] /= len, n[1] /= len, n[2] /= len;
			const float ny = std::fabs(n[1]);
			const bool  flat = ny >= STEEP_MAX || ny < STEEP_MIN;
			auto&       grid = flat ? solid : steep;

			int dom = 0;
			if (std::fabs(n[1]) > std::fabs(n[dom])) dom = 1;
			if (std::fabs(n[2]) > std::fabs(n[dom])) dom = 2;
			const int   u = (dom + 1) % 3, v = (dom + 2) % 3;
			const float d = dot(n, a);
			const float r = 0.5f * (std::fabs(n[0]) + std::fabs(n[1]) + std::fabs(n[2]));
			const int   iu0 = clamp_lo(lo[u]), iu1 = clamp_hi(hi[u]), iv0 = clamp_lo(lo[v]), iv1 = clamp_hi(hi[v]);
			const int   id0 = clamp_lo(lo[dom]), id1 = clamp_hi(hi[dom]);
			for (int iu = iu0; iu <= iu1; ++iu) {
				for (int iv = iv0; iv <= iv1; ++iv) {
					const float cu = iu + 0.5f, cv = iv + 0.5f;
					const float s0 = (d - r - n[u] * cu - n[v] * cv) / n[dom];
					const float s1 = (d + r - n[u] * cu - n[v] * cv) / n[dom];
					const int   a0 = std::max(id0, static_cast<int>(std::floor(std::min(s0, s1) - 0.5f)));
					const int   a1 = std::min(id1, static_cast<int>(std::ceil(std::max(s0, s1) - 0.5f)));
					for (int id = a0; id <= a1; ++id) {
						float cen[3];
						cen[dom] = id + 0.5f;
						cen[u] = cu;
						cen[v] = cv;
						if (tri_box_overlap(cen, 0.5f, a, b, c, n)) {
							int p[3];
							p[dom] = id, p[u] = iu, p[v] = iv;
							set(grid, p[0], p[1], p[2]);
						}
					}
				}
			}
		}

		// convex primitives: voxel-centre containment with a small margin.
		constexpr float m = PRIM_MARGIN / 8.0f;
		for (const auto& cvx : job.prims.convexes) {
			float lo[3], hi[3];
			to_voxel(cvx.lo, lo);
			to_voxel(cvx.hi, hi);
			if (hi[0] < 0 || hi[1] < 0 || hi[2] < 0 || lo[0] > G || lo[1] > G || lo[2] > G) {
				continue;
			}
			for (int y = clamp_lo(lo[1] - 1); y <= clamp_hi(hi[1] + 1); ++y) {
				for (int z = clamp_lo(lo[2] - 1); z <= clamp_hi(hi[2] + 1); ++z) {
					for (int x = clamp_lo(lo[0] - 1); x <= clamp_hi(hi[0] + 1); ++x) {
						const float p[3] = { ox + (x + 0.5f) / 8.0f, oy + (y + 0.5f) / 8.0f, oz + (z + 0.5f) / 8.0f };
						bool        inside = true;
						for (const auto& pl : cvx.planes) {
							if (pl[0] * p[0] + pl[1] * p[1] + pl[2] * p[2] + pl[3] > m) {
								inside = false;
								break;
							}
						}
						if (inside) {
							set(solid, x, y, z);
						}
					}
				}
			}
		}

		// steep (50-84 degree) surfaces: snap to whole-block footprints so the risers between
		// neighbouring columns exceed minecraft's 0.6 step height. minecraft's own step-up and jump
		// rules then decide what is climbable, like a cliff made of blocks.
		for (int by = 0; by < REGION_SIZE; ++by) {
			for (int bz = 0; bz < REGION_SIZE; ++bz) {
				for (int bx = 0; bx < REGION_SIZE; ++bx) {
					const std::uint64_t xmask = 0xFFull << (bx * 8);
					int                 min_y = 99, max_y = -1;
					for (int y = by * 8; y < by * 8 + 8; ++y) {
						for (int z = bz * 8; z < bz * 8 + 8; ++z) {
							if (steep[y * G + z] & xmask) {
								min_y = std::min(min_y, y);
								max_y = std::max(max_y, y);
							}
						}
					}
					for (int y = min_y; y <= max_y; ++y) {
						for (int z = bz * 8; z < bz * 8 + 8; ++z) {
							solid[y * G + z] |= xmask;
						}
					}
				}
			}
		}

		// pack the non-empty blocks.
		std::vector<proto::ColBlock> blocks;
		blocks.reserve(64);
		for (int by = 0; by < REGION_SIZE; ++by) {
			for (int bz = 0; bz < REGION_SIZE; ++bz) {
				for (int bx = 0; bx < REGION_SIZE; ++bx) {
					proto::ColBlock blk{};
					bool            any = false;
					for (int sy = 0; sy < 8; ++sy) {
						std::uint64_t layer = 0;
						for (int sz = 0; sz < 8; ++sz) {
							const auto row = (solid[(by * 8 + sy) * G + (bz * 8 + sz)] >> (bx * 8)) & 0xFF;
							layer |= row << (sz * 8);
						}
						blk.bits[sy] = layer;
						any |= layer != 0;
					}
					if (any) {
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

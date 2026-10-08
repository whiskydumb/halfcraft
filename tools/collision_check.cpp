// collision_check: are a map's collision voxels a floor wherever half-life's floors are? (built and run by
// tools/collision_check.ps1)
//
// minecraft's player walks on half-life's exact triangles; its mobs and items stand on the voxels server.dll
// streams with them. this reads a map's .bsp, gathers what server.dll's WorldCollision gathers from the
// world (its brushes and displacements: no props, no brush entities), voxelizes every region with the
// streamer's own code (core/hc_collision_shapes.h) and looks under every walkable triangle, every 1/16 block: a spot
// with no voxel there or right under it is a hole a mob or an item falls through.
//
//   collision_check <map.bsp> <grid z> [<source x> <source y> <radius>]
//
// grid z: the map's grid height (core/hc_grid.h), as server.dll logs it on a load ("grid offset for
// d1_canals_01: 16 units"). the optional point limits the check to a square around it (source units).
// it also times what the streamer's worker thread does with each region that has anything in it
// (triangulate and voxelize_region): the worker has to keep up with the regions server.dll gathers.
// the triangles come straight from the .bsp: the game hands them over through vphysics, which may move a
// vertex by a float's last bit.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/hc_units.h"
#include "core/hc_collision_shapes.h"

using namespace halfcraft;

namespace
{
	// ---- the .bsp (version 20: half-life 2 and its episodes) ------------------------------------
	constexpr int LUMP_PLANES = 1, LUMP_VERTEXES = 3, LUMP_NODES = 5, LUMP_FACES = 7, LUMP_LEAFS = 10, LUMP_EDGES = 12, LUMP_SURFEDGES = 13,
				  LUMP_MODELS = 14, LUMP_LEAFBRUSHES = 17, LUMP_BRUSHES = 18, LUMP_BRUSHSIDES = 19, LUMP_DISPINFO = 26, LUMP_DISP_VERTS = 33;
	// what source's player collides with (hc_world_collision.cpp's PLAYER_SOLID_BRUSHES)
	constexpr int   PLAYER_SOLID = 0x1 | 0x2 | 0x8 | 0x4000 | 0x10000;
	constexpr float WALKABLE_NY = 0.7f;      // HostTri.WALKABLE_NY
	constexpr float SAMPLE_STEP = 1.0f / 16;  // blocks between the spots looked at: two to a voxel
	constexpr float ON_BOUNDARY = 1e-3f;     // voxels

	struct Plane
	{
		float normal[3], dist;
		int   type;
	};
	struct Node
	{
		int            plane, children[2];
		short          mins[3], maxs[3];
		unsigned short first_face, face_count;
		short          area, pad;
	};
	struct Model
	{
		float mins[3], maxs[3], origin[3];
		int   head_node, first_face, face_count;
	};
	struct Brush
	{
		int first_side, side_count, contents;
	};
	struct BrushSide
	{
		unsigned short plane;
		short          texinfo, dispinfo;
		std::uint8_t   bevel, thin;
	};
	struct Face
	{
		unsigned short plane;
		std::uint8_t   side, on_node;
		int            first_edge;
		short          edge_count, texinfo, dispinfo, fog_volume;
		std::uint8_t   styles[4];
		int            light_offset;
		float          area;
		int            luxel_mins[2], luxel_size[2];
		int            original_face;
		unsigned short prim_count, first_prim;
		unsigned int   smoothing_groups;
	};
	struct DispInfo
	{
		float          start[3];
		int            first_vert, first_tri, power, min_tess;
		float          smoothing_angle;
		int            contents;
		unsigned short face;
		std::uint8_t   rest[176 - 38];
	};
	struct DispVert
	{
		float vector[3], dist, alpha;
	};
	static_assert(sizeof(Plane) == 20 && sizeof(Node) == 32 && sizeof(Model) == 48 && sizeof(Brush) == 12 && sizeof(BrushSide) == 8);
	static_assert(sizeof(Face) == 56 && sizeof(DispInfo) == 176 && sizeof(DispVert) == 20);

	struct Bsp
	{
		std::vector<std::uint8_t> bytes;

		bool read(const char* path)
		{
			FILE* file = nullptr;
			if (fopen_s(&file, path, "rb") != 0 || !file) {
				return false;
			}
			std::fseek(file, 0, SEEK_END);
			bytes.resize(static_cast<std::size_t>(std::ftell(file)));
			std::fseek(file, 0, SEEK_SET);
			const bool ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
			std::fclose(file);
			return ok && bytes.size() > 1036 && std::memcmp(bytes.data(), "VBSP", 4) == 0;
		}

		int version(int lump) const { return field(lump, 2); }

		template <typename T>
		std::vector<T> lump(int index, std::size_t size = sizeof(T)) const
		{
			std::vector<T> out(static_cast<std::size_t>(field(index, 1)) / size);
			for (std::size_t i = 0; i < out.size(); ++i) {
				std::memcpy(&out[i], bytes.data() + field(index, 0) + i * size, std::min(size, sizeof(T)));
			}
			return out;
		}

	private:
		int field(int lump, int k) const
		{
			int v;
			std::memcpy(&v, bytes.data() + 8 + lump * 16 + k * 4, 4);
			return v;
		}
	};

	// ---- the world as server.dll's WorldCollision gathers it ------------------------------------
	struct World
	{
		std::vector<std::vector<std::array<float, 4>>> brushes;  // source planes: inside where n.p <= dist
		std::vector<std::array<float, 6>>              brush_bounds;
		std::vector<std::array<float, 9>>              disp_tris;  // source corners
		float                                          mins[3], maxs[3];
	};

	void read_world(const Bsp& bsp, World& world)
	{
		const auto planes = bsp.lump<Plane>(LUMP_PLANES);
		const auto nodes = bsp.lump<Node>(LUMP_NODES);
		const auto models = bsp.lump<Model>(LUMP_MODELS);
		const auto brushes = bsp.lump<Brush>(LUMP_BRUSHES);
		const auto sides = bsp.lump<BrushSide>(LUMP_BRUSHSIDES);
		const auto leaf_brushes = bsp.lump<unsigned short>(LUMP_LEAFBRUSHES);
		const auto vertexes = bsp.lump<std::array<float, 3>>(LUMP_VERTEXES);
		const auto edges = bsp.lump<std::array<unsigned short, 2>>(LUMP_EDGES);
		const auto surfedges = bsp.lump<int>(LUMP_SURFEDGES);
		const auto faces = bsp.lump<Face>(LUMP_FACES);
		const auto disps = bsp.lump<DispInfo>(LUMP_DISPINFO);
		const auto disp_verts = bsp.lump<DispVert>(LUMP_DISP_VERTS);
		// dleaf_t: version 0 carries ambient lighting, version 1 (half-life 2's) doesn't
		struct LeafHead
		{
			int            contents;
			short          cluster, area_flags, mins[3], maxs[3];
			unsigned short first_face, face_count, first_brush, brush_count;
		};
		const auto leaves = bsp.lump<LeafHead>(LUMP_LEAFS, bsp.version(LUMP_LEAFS) == 0 ? 56 : 32);
		for (int k = 0; k < 3; ++k) {
			world.mins[k] = models[0].mins[k];
			world.maxs[k] = models[0].maxs[k];
		}

		// the world's brushes: those in the leaves under model 0's head node (brush entities have their own)
		std::set<int>    used;
		std::vector<int> stack{ models[0].head_node };
		while (!stack.empty()) {
			const int node = stack.back();
			stack.pop_back();
			if (node < 0) {
				const auto& leaf = leaves[static_cast<std::size_t>(-1 - node)];
				for (int i = 0; i < leaf.brush_count; ++i) {
					used.insert(leaf_brushes[leaf.first_brush + i]);
				}
				continue;
			}
			stack.push_back(nodes[node].children[0]);
			stack.push_back(nodes[node].children[1]);
		}
		for (const int b : used) {
			if (!(brushes[b].contents & PLAYER_SOLID)) {
				continue;
			}
			std::vector<std::array<float, 4>> brush;
			std::array<float, 6>              bounds{ -1e9f, -1e9f, -1e9f, 1e9f, 1e9f, 1e9f };
			for (int s = 0; s < brushes[b].side_count; ++s) {
				const Plane& p = planes[sides[brushes[b].first_side + s].plane];
				brush.push_back({ p.normal[0], p.normal[1], p.normal[2], p.dist });
				for (int axis = 0; axis < 3; ++axis) {
					if (p.normal[axis] == 1.0f) {
						bounds[3 + axis] = std::min(bounds[3 + axis], p.dist);
					} else if (p.normal[axis] == -1.0f) {
						bounds[axis] = std::max(bounds[axis], -p.dist);
					}
				}
			}
			world.brushes.push_back(std::move(brush));
			world.brush_bounds.push_back(bounds);
		}

		// displacements: the face's corners (the one nearest the start first) spanned by a
		// (2^power + 1)^2 grid, each point pushed out along its vector; valve's alternating diagonals
		for (const auto& disp : disps) {
			const Face& face = faces[disp.face];
			if (face.edge_count != 4 || disp.power < 1 || disp.power > 4) {
				continue;
			}
			std::array<float, 3> corner[4];
			for (int i = 0; i < 4; ++i) {
				const int edge = surfedges[face.first_edge + i];
				corner[i] = vertexes[edge >= 0 ? edges[edge][0] : edges[-edge][1]];
			}
			int   first = 0;
			float best = 1e30f;
			for (int i = 0; i < 4; ++i) {
				float d = 0.0f;
				for (int k = 0; k < 3; ++k) {
					d += (corner[i][k] - disp.start[k]) * (corner[i][k] - disp.start[k]);
				}
				if (d < best) {
					best = d;
					first = i;
				}
			}
			std::array<float, 3> c[4];
			for (int i = 0; i < 4; ++i) {
				c[i] = corner[(first + i) % 4];
			}
			const int                         side = (1 << disp.power) + 1;
			const float                       step = 1.0f / static_cast<float>(side - 1);
			std::vector<std::array<float, 3>> grid(static_cast<std::size_t>(side * side));
			for (int row = 0; row < side; ++row) {
				for (int col = 0; col < side; ++col) {
					const DispVert& v = disp_verts[disp.first_vert + row * side + col];
					for (int k = 0; k < 3; ++k) {
						const float left = c[0][k] + (c[1][k] - c[0][k]) * (row * step);
						const float right = c[3][k] + (c[2][k] - c[3][k]) * (row * step);
						grid[row * side + col][k] = left + (right - left) * (col * step) + v.vector[k] * v.dist;
					}
				}
			}
			auto add = [&](int a, int b, int d) {
				world.disp_tris.push_back({ grid[a][0], grid[a][1], grid[a][2], grid[b][0], grid[b][1], grid[b][2], grid[d][0], grid[d][1], grid[d][2] });
			};
			for (int row = 0; row + 1 < side; ++row) {
				for (int col = 0; col + 1 < side; ++col) {
					const int i = row * side + col;
					if (i % 2 == 1) {
						add(i, i + side, i + 1);
						add(i + 1, i + side, i + side + 1);
					} else {
						add(i, i + side, i + side + 1);
						add(i, i + side + 1, i + 1);
					}
				}
			}
		}
	}

	// ---- one region, as CollisionStreamer::harvest + WorldCollision::gather make it --------------
	void to_mc(const float* source, MapSlot slot, float* out)
	{
		const McVec mc = source_to_mc(source, slot);
		out[0] = static_cast<float>(mc.x);
		out[1] = static_cast<float>(mc.y);
		out[2] = static_cast<float>(mc.z);
	}

	void gather(const World& world, MapSlot slot, int rx, int ry, int rz, ColPrimitives& out)
	{
		const float units = static_cast<float>(UNITS_PER_BLOCK);
		const float lo[3] = { float(rx * COLLISION_REGION_SIZE) - 0.5f, float(ry * COLLISION_REGION_SIZE) - 0.5f, float(rz * COLLISION_REGION_SIZE) - 0.5f };
		const float hi[3] = { lo[0] + COLLISION_REGION_SIZE + 1.0f, lo[1] + COLLISION_REGION_SIZE + 1.0f, lo[2] + COLLISION_REGION_SIZE + 1.0f };
		const float mins[3] = { lo[0] * units, -hi[2] * units, lo[1] * units + slot.grid_z };
		const float maxs[3] = { hi[0] * units, -lo[2] * units, hi[1] * units + slot.grid_z };
		for (std::size_t b = 0; b < world.brushes.size(); ++b) {
			const auto& bounds = world.brush_bounds[b];
			if (bounds[0] > maxs[0] || bounds[3] < mins[0] || bounds[1] > maxs[1] || bounds[4] < mins[1] || bounds[2] > maxs[2] || bounds[5] < mins[2]) {
				continue;
			}
			ColPrimitives::Convex cvx;
			brush_to_convex(world.brushes[b].data(), world.brushes[b].size(), mins, maxs, slot, cvx);
			out.convexes.push_back(std::move(cvx));
		}
		for (const auto& t : world.disp_tris) {
			float tlo[3], thi[3];
			for (int k = 0; k < 3; ++k) {
				tlo[k] = std::min({ t[k], t[3 + k], t[6 + k] });
				thi[k] = std::max({ t[k], t[3 + k], t[6 + k] });
			}
			if (tlo[0] > maxs[0] || thi[0] < mins[0] || tlo[1] > maxs[1] || thi[1] < mins[1] || tlo[2] > maxs[2] || thi[2] < mins[2]) {
				continue;
			}
			ColPrimitives::Tri tri;
			for (int v = 0; v < 3; ++v) {
				to_mc(t.data() + v * 3, slot, tri.v + v * 3);
			}
			out.tris.push_back(tri);
		}
	}

	/// (x, z) lies inside the triangle seen from above (its edges included).
	bool covers(const float* v, float x, float z)
	{
		auto        side = [&](int a, int b) { return (v[b * 3] - v[a * 3]) * (z - v[a * 3 + 2]) - (v[b * 3 + 2] - v[a * 3 + 2]) * (x - v[a * 3]); };
		const float d0 = side(0, 1), d1 = side(1, 2), d2 = side(2, 0);
		return (d0 >= 0.0f && d1 >= 0.0f && d2 >= 0.0f) || (d0 <= 0.0f && d1 <= 0.0f && d2 <= 0.0f);
	}

	struct Hole
	{
		double x, y, z;  // source units
		int    spots;
	};
}

int check(int argc, char** argv)
{
	if (argc != 3 && argc != 6) {
		std::fprintf(stderr, "usage: collision_check <map.bsp> <grid z> [<source x> <source y> <radius>]\n");
		return 2;
	}
	Bsp bsp;
	if (!bsp.read(argv[1])) {
		std::fprintf(stderr, "can't read %s as a .bsp\n", argv[1]);
		return 2;
	}
	MapSlot slot;
	slot.grid_z = static_cast<float>(std::atof(argv[2]));
	World world;
	read_world(bsp, world);
	if (argc == 6) {
		const float x = static_cast<float>(std::atof(argv[3])), y = static_cast<float>(std::atof(argv[4])), r = static_cast<float>(std::atof(argv[5]));
		world.mins[0] = std::max(world.mins[0], x - r);
		world.maxs[0] = std::min(world.maxs[0], x + r);
		world.mins[1] = std::max(world.mins[1], y - r);
		world.maxs[1] = std::min(world.maxs[1], y + r);
	}
	std::printf("%zu world brushes, %zu displacement triangles; grid z %.0f\n", world.brushes.size(), world.disp_tris.size(), slot.grid_z);

	float mc_lo[3], mc_hi[3];
	to_mc(world.mins, slot, mc_lo);
	to_mc(world.maxs, slot, mc_hi);
	for (int k = 0; k < 3; ++k) {
		if (mc_lo[k] > mc_hi[k]) {
			std::swap(mc_lo[k], mc_hi[k]);
		}
	}
	auto      region_of = [](float v) { return static_cast<int>(std::floor(v / COLLISION_REGION_SIZE)); };
	const int rx0 = region_of(mc_lo[0]), rx1 = region_of(mc_hi[0]);
	const int ry0 = region_of(mc_lo[1]), ry1 = region_of(mc_hi[1]);
	const int rz0 = region_of(mc_lo[2]), rz1 = region_of(mc_hi[2]);

	std::uint64_t                      spots = 0, holes = 0, flat_on_boundary = 0;
	std::map<std::array<int, 3>, Hole> holes_by_block;  // minecraft block -> its holes
	std::vector<ColPrimitives::Tri>    tris;
	using Clock = std::chrono::steady_clock;
	Clock::duration worker{}, slowest{};
	std::uint64_t   timed = 0;
	for (int rx = rx0; rx <= rx1; ++rx) {
		for (int rz = rz0; rz <= rz1; ++rz) {
			// a column of regions at a time: a spot right on a region's top looks into the one above
			std::map<int, RegionVoxels>  column;
			std::map<int, ColPrimitives> prims;
			for (int ry = ry0; ry <= ry1; ++ry) {
				gather(world, slot, rx, ry, rz, prims[ry]);
				const auto began = Clock::now();
				tris.clear();
				triangulate(prims[ry], tris);
				voxelize_region(prims[ry], rx, ry, rz, column[ry]);
				if (!prims[ry].tris.empty() || !prims[ry].convexes.empty()) {
					const auto took = Clock::now() - began;
					worker += took;
					slowest = std::max(slowest, took);
					++timed;
				}
			}
			auto solid_at = [&](int ry, int vx, int vy, int vz) {
				ry += static_cast<int>(std::floor(vy / static_cast<float>(REGION_VOXELS)));
				vy = ((vy % REGION_VOXELS) + REGION_VOXELS) % REGION_VOXELS;
				const auto it = column.find(ry);
				return it != column.end() && (it->second[vy * REGION_VOXELS + vz] >> vx & 1) != 0;
			};
			for (int ry = ry0; ry <= ry1; ++ry) {
				tris.clear();
				triangulate(prims[ry], tris);
				const std::size_t from_disp = prims[ry].tris.size();
				const float       ox = float(rx * COLLISION_REGION_SIZE), oy = float(ry * COLLISION_REGION_SIZE), oz = float(rz * COLLISION_REGION_SIZE);
				for (std::size_t i = 0; i < tris.size(); ++i) {
					const float* v = tris[i].v;
					const float  e1[3] = { v[3] - v[0], v[4] - v[1], v[5] - v[2] }, e2[3] = { v[6] - v[0], v[7] - v[1], v[8] - v[2] };
					const float  n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
					const float  len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
					// a brush's faces wind outwards: its floors face up. a displacement comes in either winding
					const float ny = len > 0.0f ? n[1] / len : 0.0f;
					if ((i < from_disp ? std::fabs(ny) : ny) < WALKABLE_NY) {
						continue;
					}
					if (v[1] == v[4] && v[4] == v[7] && v[1] * 8.0f == std::floor(v[1] * 8.0f) && v[0] >= ox && v[0] < ox + COLLISION_REGION_SIZE && v[2] >= oz &&
						v[2] < oz + COLLISION_REGION_SIZE && v[1] > oy && v[1] <= oy + COLLISION_REGION_SIZE) {
						++flat_on_boundary;
					}
					// the spots of this region it covers, seen from above, every 1/16 block (between voxel edges)
					const float x0 = std::max(std::min({ v[0], v[3], v[6] }), ox), x1 = std::min(std::max({ v[0], v[3], v[6] }), ox + COLLISION_REGION_SIZE);
					const float z0 = std::max(std::min({ v[2], v[5], v[8] }), oz), z1 = std::min(std::max({ v[2], v[5], v[8] }), oz + COLLISION_REGION_SIZE);
					for (float x = (std::floor(x0 / SAMPLE_STEP) + 0.5f) * SAMPLE_STEP; x < x1; x += SAMPLE_STEP) {
						for (float z = (std::floor(z0 / SAMPLE_STEP) + 0.5f) * SAMPLE_STEP; z < z1; z += SAMPLE_STEP) {
							if (x < x0 || z < z0 || !covers(v, x, z)) {
								continue;
							}
							const float y = v[1] - (n[0] * (x - v[0]) + n[2] * (z - v[2])) / n[1];
							// each spot once: in the region it's in (on a region's floor, the one below)
							if (y <= oy || y > oy + COLLISION_REGION_SIZE) {
								continue;
							}
							const float vy = (y - oy) * 8.0f;
							const int   vx = static_cast<int>(std::floor((x - ox) * 8.0f)), vz = static_cast<int>(std::floor((z - oz) * 8.0f));
							++spots;
							// held up: a voxel at the spot or right under it (on a slope the voxels' steps sit up to
							// one under the surface)
							const int top = static_cast<int>(std::floor(vy + ON_BOUNDARY));
							if (solid_at(ry, vx, top, vz) || solid_at(ry, vx, top - 1, vz)) {
								continue;
							}
							++holes;
							const std::array<int, 3> block{ static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y)), static_cast<int>(std::floor(z)) };
							auto&                    hole = holes_by_block[block];
							if (hole.spots++ == 0) {
								float source[3];
								mc_to_source(x, y, z, slot, source);
								hole.x = source[0], hole.y = source[1], hole.z = source[2];
							}
						}
					}
				}
			}
		}
	}

	std::printf("%llu walkable spots checked (every 1/16 block), %llu with no voxel under them (%zu blocks)\n", static_cast<unsigned long long>(spots),
		static_cast<unsigned long long>(holes), holes_by_block.size());
	std::printf("%llu walkable triangles lie flat exactly on a voxel boundary\n", static_cast<unsigned long long>(flat_on_boundary));
	auto ms = [](Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); };
	std::printf("worker: %llu regions with geometry, %.2f ms a region on average, %.2f ms at worst\n", static_cast<unsigned long long>(timed),
		timed ? ms(worker) / static_cast<double>(timed) : 0.0, ms(slowest));
	std::vector<Hole> worst;
	worst.reserve(holes_by_block.size());
	for (const auto& entry : holes_by_block) {
		worst.push_back(entry.second);
	}
	std::sort(worst.begin(), worst.end(), [](const Hole& a, const Hole& b) { return a.spots > b.spots; });
	for (std::size_t i = 0; i < worst.size() && i < 20; ++i) {
		std::printf("  hole at source (%.0f, %.0f, %.0f): %d spots in its block\n", worst[i].x, worst[i].y, worst[i].z, worst[i].spots);
	}
	return holes == 0 ? 0 : 1;
}

int main(int argc, char** argv)
{
	// a map the bsp reader chokes on (a bad lump size, out of memory) says so instead of crashing
	try {
		return check(argc, argv);
	} catch (const std::exception& error) {
		std::fprintf(stderr, "collision_check: %s\n", error.what());
		return 2;
	}
}

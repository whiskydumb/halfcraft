// client.dll and server.dll: a map's most common floor, read from its .bsp (see hc_floors.h).
//
// "most common" means where the map's characters stand: every ground node (info_node,
// info_node_hint) and player spawn votes for the floor under it when that floor is flat. counting
// floor area instead picks rooftops, hidden rooms and the 3d skybox (d1_canals_01's largest flat
// floors are rooms nobody enters). a map without any of them falls back to the area of its flat floors.
//
// only the world's faces count (brushes and displacements, as source draws them): not water, sky,
// nodraw or tool faces, not props or brush entities.

#include "cbase.h"
#include "bspfile.h"
#include "filesystem.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/hc_grid.h"
#include "core/hc_log.h"
#include "core/hc_units.h"
#include "shared/hc_bsp.h"
#include "shared/hc_floors.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		// @note: the grid offset isn't stored anywhere, so these rules (and FloorVotes' ties in
		// core/hc_grid.h) are part of every world's format: changing any of them can move all the
		// builds on a map by up to 36 units. several maps are decided by a vote or two (ep1_c17_05:
		// 163 to 162, ep2_outland_01: 46 to 44, d1_canals_03: 28 to 27), so even a small tweak does.
		constexpr float WALKABLE_NORMAL_Z = 0.7f;  // source's own limit for standing on a slope
		constexpr float FLAT_NORMAL_Z = 0.9999f;   // under a degree: a block sits flush on it
		constexpr float NODE_SUNK = 16.0f;         // a node's origin can be this far into its floor
		constexpr float NODE_HOVER = 128.0f;       // or this far above it (hint nodes on crates, spawns in the air)
		constexpr float CELL_UNITS = 256.0f;       // bucket size for finding the floor under a point
		constexpr int   NOT_A_FLOOR = SURF_SKY2D | SURF_SKY | SURF_WARP | SURF_TRIGGER | SURF_NODRAW | SURF_HINT | SURF_SKIP;
		constexpr int   MAX_DISP_POWER = 4;

		// what characters stand on: npcs' ground nodes, and where players spawn (campaign and deathmatch)
		constexpr const char* GROUND_CLASSES[] = {
			"info_node", "info_node_hint", "info_player_start", "info_player_deathmatch", "info_player_combine", "info_player_rebel",
		};

		using bsp::BspFile;

		/// a world triangle a character can stand on.
		struct Tri
		{
			Vector a, b, c;
			float  lo[2], hi[2];  // xy bounds
			bool   flat;
			float  area;
		};

		/// keeps a triangle if it faces up enough to stand on.
		/// @param up - the face's normal: the triangle faces the same way, whichever way it winds
		void add_tri(const Vector& a, const Vector& b, const Vector& c, const Vector& up, std::vector<Tri>& out)
		{
			Vector      n = CrossProduct(b - a, c - a);
			const float length = n.Length();
			if (length < 1e-3f) {
				return;
			}
			if (DotProduct(n, up) < 0.0f) {
				n = -n;
			}
			const float nz = n.z / length;
			if (nz < WALKABLE_NORMAL_Z) {
				return;
			}
			Tri tri{ a, b, c, { std::min({ a.x, b.x, c.x }), std::min({ a.y, b.y, c.y }) }, { std::max({ a.x, b.x, c.x }), std::max({ a.y, b.y, c.y }) },
				nz >= FLAT_NORMAL_Z, length * 0.5f };
			out.push_back(tri);
		}

		/// a displacement's surface as triangles: the face's corners (the one nearest startPosition
		/// first) spanned by a (2^power + 1)^2 grid, each point pushed out along its vector.
		void add_displacement(const ddispinfo_t& disp, const std::vector<CDispVert>& verts, const Vector corners_in[4], const Vector& up, std::vector<Tri>& out)
		{
			if (disp.power < 1 || disp.power > MAX_DISP_POWER) {
				return;
			}
			const int side = (1 << disp.power) + 1;
			if (disp.m_iDispVertStart < 0 || static_cast<std::size_t>(disp.m_iDispVertStart) + side * side > verts.size()) {
				return;
			}
			int first = 0;
			for (int i = 1; i < 4; ++i) {
				if (corners_in[i].DistToSqr(disp.startPosition) < corners_in[first].DistToSqr(disp.startPosition)) {
					first = i;
				}
			}
			Vector corners[4];
			for (int i = 0; i < 4; ++i) {
				corners[i] = corners_in[(first + i) % 4];
			}
			std::vector<Vector> grid(side * side);
			const float         step = 1.0f / static_cast<float>(side - 1);
			for (int row = 0; row < side; ++row) {
				const Vector left = corners[0] + (corners[1] - corners[0]) * (row * step);
				const Vector right = corners[3] + (corners[2] - corners[3]) * (row * step);
				for (int col = 0; col < side; ++col) {
					const CDispVert& v = verts[disp.m_iDispVertStart + row * side + col];
					grid[row * side + col] = left + (right - left) * (col * step) + v.m_vVector * v.m_flDist;
				}
			}
			for (int row = 0; row + 1 < side; ++row) {
				for (int col = 0; col + 1 < side; ++col) {
					const Vector& p00 = grid[row * side + col];
					const Vector& p01 = grid[row * side + col + 1];
					const Vector& p10 = grid[(row + 1) * side + col];
					const Vector& p11 = grid[(row + 1) * side + col + 1];
					add_tri(p00, p01, p11, up, out);
					add_tri(p00, p11, p10, up, out);
				}
			}
		}

		/// the world's walkable triangles. false when the map's lumps can't be read.
		bool read_floors(BspFile& bsp, std::vector<Tri>& out)
		{
			std::vector<dplane_t>    planes;
			std::vector<dvertex_t>   vertices;
			std::vector<texinfo_t>   texinfos;
			std::vector<dface_t>     faces;
			std::vector<dedge_t>     edges;
			std::vector<int>         surfedges;
			std::vector<dmodel_t>    models;
			std::vector<ddispinfo_t> disps;
			std::vector<CDispVert>   disp_verts;
			if (!bsp.lump(LUMP_PLANES, planes) || !bsp.lump(LUMP_VERTEXES, vertices) || !bsp.lump(LUMP_TEXINFO, texinfos) || !bsp.lump(LUMP_EDGES, edges) ||
				!bsp.lump(LUMP_SURFEDGES, surfedges) || !bsp.lump(LUMP_MODELS, models) || !bsp.lump(LUMP_DISPINFO, disps) ||
				!bsp.lump(LUMP_DISP_VERTS, disp_verts)) {
				return false;
			}
			// maps lit for hdr only can leave the ldr face lump empty
			if (!bsp.lump(LUMP_FACES, faces) || (faces.empty() && !bsp.lump(LUMP_FACES_HDR, faces))) {
				return false;
			}
			if (models.empty() || models[0].firstface < 0 || static_cast<std::size_t>(models[0].firstface) + models[0].numfaces > faces.size()) {
				return false;
			}

			std::vector<Vector> polygon;
			for (int f = models[0].firstface; f < models[0].firstface + models[0].numfaces; ++f) {
				const dface_t& face = faces[f];
				if (face.planenum >= planes.size() || face.texinfo < 0 || static_cast<std::size_t>(face.texinfo) >= texinfos.size() ||
					(texinfos[face.texinfo].flags & NOT_A_FLOOR)) {
					continue;
				}
				const Vector up = face.side ? -planes[face.planenum].normal : planes[face.planenum].normal;
				if (face.dispinfo < 0 && up.z < WALKABLE_NORMAL_Z) {
					continue;
				}
				polygon.clear();
				for (int e = 0; e < face.numedges; ++e) {
					const std::size_t index = static_cast<std::size_t>(face.firstedge) + e;
					if (face.firstedge < 0 || index >= surfedges.size()) {
						break;
					}
					const int         surfedge = surfedges[index];
					const std::size_t edge = static_cast<std::size_t>(std::abs(surfedge));
					if (edge >= edges.size()) {
						break;
					}
					const unsigned short vertex = surfedge >= 0 ? edges[edge].v[0] : edges[edge].v[1];
					if (vertex >= vertices.size()) {
						break;
					}
					polygon.push_back(vertices[vertex].point);
				}
				if (polygon.size() != static_cast<std::size_t>(face.numedges) || polygon.size() < 3) {
					continue;
				}
				if (face.dispinfo >= 0) {
					if (static_cast<std::size_t>(face.dispinfo) < disps.size() && polygon.size() == 4) {
						add_displacement(disps[face.dispinfo], disp_verts, polygon.data(), up, out);
					}
					continue;
				}
				for (std::size_t i = 1; i + 1 < polygon.size(); ++i) {
					add_tri(polygon[0], polygon[i], polygon[i + 1], up, out);
				}
			}
			return true;
		}

		bool is_ground_class(const std::string& classname)
		{
			for (const char* ground : GROUND_CLASSES) {
				if (Q_stricmp(classname.c_str(), ground) == 0) {
					return true;
				}
			}
			return false;
		}

		/// the origins of the entities characters stand at, from the entity lump's text.
		std::vector<Vector> ground_points(const std::vector<char>& text)
		{
			std::vector<Vector> out;
			std::string         key, classname, origin;
			bool                is_value = false;
			for (std::size_t i = 0; i < text.size(); ++i) {
				const char ch = text[i];
				if (ch == '{') {
					classname.clear();
					origin.clear();
					is_value = false;
				} else if (ch == '}') {
					if (is_ground_class(classname)) {
						char*        end = nullptr;
						const char*  at = origin.c_str();
						const float  x = std::strtof(at, &end);
						const char*  after_x = end;
						const float  y = std::strtof(after_x, &end);
						const char*  after_y = end;
						const float  z = std::strtof(after_y, &end);
						if (after_x != at && after_y != after_x && end != after_y) {
							out.emplace_back(x, y, z);
						}
					}
				} else if (ch == '"') {
					std::size_t close = i + 1;
					while (close < text.size() && text[close] != '"') {
						++close;
					}
					if (close >= text.size()) {
						break;
					}
					std::string token(text.data() + i + 1, close - i - 1);
					i = close;
					if (!is_value) {
						key = std::move(token);
					} else if (key == "classname") {
						classname = std::move(token);
					} else if (key == "origin") {
						origin = std::move(token);
					}
					is_value = !is_value;
				}
			}
			return out;
		}

		/// the walkable triangles in buckets on the xy plane.
		class FloorIndex
		{
		public:
			explicit FloorIndex(const std::vector<Tri>& tris) : tris_(tris)
			{
				for (int i = 0; i < static_cast<int>(tris.size()); ++i) {
					const Tri& t = tris[i];
					for (int cx = cell(t.lo[0]); cx <= cell(t.hi[0]); ++cx) {
						for (int cy = cell(t.lo[1]); cy <= cell(t.hi[1]); ++cy) {
							cells_[key(cx, cy)].push_back(i);
						}
					}
				}
			}

			/// the highest triangle under a point, from NODE_SUNK above it down to NODE_HOVER below.
			/// @param height - set to the triangle's height under the point
			const Tri* under(const Vector& p, float& height) const
			{
				const auto it = cells_.find(key(cell(p.x), cell(p.y)));
				if (it == cells_.end()) {
					return nullptr;
				}
				const Tri* best = nullptr;
				for (const int i : it->second) {
					const Tri& t = tris_[i];
					float      h;
					if (p.x < t.lo[0] || p.x > t.hi[0] || p.y < t.lo[1] || p.y > t.hi[1] || !height_at(t, p.x, p.y, h)) {
						continue;
					}
					if (h <= p.z + NODE_SUNK && h >= p.z - NODE_HOVER && (!best || h > height)) {
						best = &t;
						height = h;
					}
				}
				return best;
			}

		private:
			static int cell(float v) { return static_cast<int>(std::floor(v / CELL_UNITS)); }

			static std::int64_t key(int cx, int cy) { return (static_cast<std::int64_t>(cx) << 32) ^ static_cast<std::uint32_t>(cy); }

			/// the triangle's height at (x, y), false when the point is outside it.
			static bool height_at(const Tri& t, float x, float y, float& out)
			{
				const float d = (t.b.y - t.c.y) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.y - t.c.y);
				if (std::fabs(d) < 1e-6f) {
					return false;
				}
				const float l1 = ((t.b.y - t.c.y) * (x - t.c.x) + (t.c.x - t.b.x) * (y - t.c.y)) / d;
				const float l2 = ((t.c.y - t.a.y) * (x - t.c.x) + (t.a.x - t.c.x) * (y - t.c.y)) / d;
				const float l3 = 1.0f - l1 - l2;
				if (l1 < -1e-4f || l2 < -1e-4f || l3 < -1e-4f) {
					return false;
				}
				out = l1 * t.a.z + l2 * t.b.z + l3 * t.c.z;
				return true;
			}

			const std::vector<Tri>&                            tris_;
			std::unordered_map<std::int64_t, std::vector<int>> cells_;
		};
	}

	float map_grid_z(const char* map_name)
	{
		const auto        start = std::chrono::steady_clock::now();
		const std::string name = map_base_name(map_name);
		const std::string path = "maps/" + name + ".bsp";

		BspFile           bsp(path.c_str());
		std::vector<Tri>  tris;
		std::vector<char> entities;
		if (!bsp.open() || !read_floors(bsp, tris) || !bsp.lump(LUMP_ENTITIES, entities)) {
			log_warning("grid offset for %s: can't read %s, the block grid stays at 0", name.c_str(), path.c_str());
			return 0.0f;
		}

		FloorVotes  votes;
		int         standing = 0;
		const auto  points = ground_points(entities);
		FloorIndex  index(tris);
		for (const Vector& point : points) {
			float      height = 0.0f;
			const Tri* floor = index.under(point, height);
			if (floor && floor->flat) {
				votes.add(height, 1.0);
				++standing;
			}
		}
		const bool by_area = votes.empty();
		if (by_area) {
			for (const Tri& t : tris) {
				if (t.flat) {
					votes.add((t.a.z + t.b.z + t.c.z) / 3.0f, t.area);
				}
			}
		}
		if (votes.empty()) {
			log_info("grid offset for %s: 0 units (no flat floor)", name.c_str());
			return 0.0f;
		}

		const auto pick = votes.pick();
		const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		if (by_area) {
			log_info("grid offset for %s: %d units (floor at %d; %.0f%% of the flat floor area, no ground nodes; %d ms)", name.c_str(), pick.grid_z,
				pick.floor_z, pick.share * 100.0, static_cast<int>(ms));
		} else {
			log_info("grid offset for %s: %d units (floor at %d; %.0f%% of %d ground nodes and spawns on flat floors; %d ms)", name.c_str(), pick.grid_z,
				pick.floor_z, pick.share * 100.0, standing, static_cast<int>(ms));
		}
		return static_cast<float>(pick.grid_z);
	}
}

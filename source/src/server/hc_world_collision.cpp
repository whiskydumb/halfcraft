// server.dll: half-life 2's collision, as minecraft geometry (see hc_world_collision.h).

#include "cbase.h"
#include "engine/IEngineTrace.h"
#include "engine/IStaticPropMgr.h"
#include "engine/ivmodelinfo.h"
#include "vphysics_interface.h"
#include "physics_shared.h"
#include "gamerules.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>

#include "server/hc_block_solids.h"
#include "server/hc_world_collision.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr int   MAX_ENTITIES = 1024;
		constexpr float MOVED_UNITS = 0.5f;
		constexpr float MOVED_DEGREES = 0.5f;
		// what source's player movement collides with (MASK_PLAYERSOLID without monsters: no brush is one)
		constexpr int PLAYER_SOLID_BRUSHES = CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_PLAYERCLIP | CONTENTS_WINDOW | CONTENTS_GRATE;

		float slot_offset(MapSlot slot)
		{
			return static_cast<float>(slot.x_blocks());
		}

		void to_mc(const Vector& source, MapSlot slot, float out[3])
		{
			const float p[3] = { source.x, source.y, source.z };
			const auto  mc = source_to_mc(p, slot);
			out[0] = static_cast<float>(mc.x);
			out[1] = static_cast<float>(mc.y);
			out[2] = static_cast<float>(mc.z);
		}

		/// a convex piece given as triangles (minecraft space) -> its planes and bounds.
		bool hull_to_convex(const std::vector<std::array<float, 3>>& corners, std::uint32_t flags, ColPrimitives::Convex& out)
		{
			if (corners.size() < 12) {
				return false;  // fewer than four triangles has no volume
			}
			float centre[3] = { 0.0f, 0.0f, 0.0f };
			out.lo[0] = out.lo[1] = out.lo[2] = FLT_MAX;
			out.hi[0] = out.hi[1] = out.hi[2] = -FLT_MAX;
			for (const auto& c : corners) {
				for (int k = 0; k < 3; ++k) {
					centre[k] += c[k];
					out.lo[k] = std::min(out.lo[k], c[k]);
					out.hi[k] = std::max(out.hi[k], c[k]);
				}
			}
			for (float& k : centre) {
				k /= static_cast<float>(corners.size());
			}
			out.planes.clear();
			for (std::size_t i = 0; i + 2 < corners.size(); i += 3) {
				const auto& a = corners[i];
				const auto& b = corners[i + 1];
				const auto& c = corners[i + 2];
				const float e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] };
				const float e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
				float       n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
				const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
				if (len < 1e-7f) {
					continue;
				}
				for (float& k : n) {
					k /= len;
				}
				float d = -(n[0] * a[0] + n[1] * a[1] + n[2] * a[2]);
				if (n[0] * centre[0] + n[1] * centre[1] + n[2] * centre[2] + d > 0.0f) {
					for (float& k : n) {
						k = -k;
					}
					d = -d;
				}
				const bool duplicate = std::any_of(out.planes.begin(), out.planes.end(), [&](const std::array<float, 4>& p) {
					return p[0] * n[0] + p[1] * n[1] + p[2] * n[2] > 0.9999f && std::fabs(p[3] - d) < 1e-4f;
				});
				if (!duplicate) {
					out.planes.push_back({ n[0], n[1], n[2], d });
				}
			}
			out.flags = flags;
			return out.planes.size() >= 4;
		}

		bool moved(const Vector& a, const Vector& b, float tolerance)
		{
			return std::fabs(a.x - b.x) > tolerance || std::fabs(a.y - b.y) > tolerance || std::fabs(a.z - b.z) > tolerance;
		}

		bool turned(const QAngle& a, const QAngle& b)
		{
			return std::fabs(a.x - b.x) > MOVED_DEGREES || std::fabs(a.y - b.y) > MOVED_DEGREES || std::fabs(a.z - b.z) > MOVED_DEGREES;
		}

		/// solid things the player collides with that minecraft should see as walls.
		bool is_wall(CBaseEntity* entity)
		{
			if (!entity || entity->IsPlayer() || entity->MyNPCPointer() || entity->IsEFlagSet(EFL_KILLME)) {
				return false;
			}
			if (FClassnameIs(entity, BLOCKS_CLASSNAME)) {
				return false;  // minecraft's own blocks: it has them already
			}
			if (!entity->IsSolid() || entity->IsSolidFlagSet(FSOLID_NOT_SOLID)) {
				return false;
			}
			// what the player carries floats in front of them: minecraft would walk into it
			if (IPhysicsObject* body = entity->VPhysicsGetObject(); body && (body->GetGameFlags() & FVPHYSICS_PLAYER_HELD)) {
				return false;
			}
			return g_pGameRules->ShouldCollide(COLLISION_GROUP_PLAYER_MOVEMENT, entity->GetCollisionGroup());
		}
	}

	void WorldCollision::reset(MapSlot slot)
	{
		slot_ = slot;
		hulls_.clear();
		movers_.clear();
	}

	void WorldCollision::to_source_box(const float lo[3], const float hi[3], Vector& mins, Vector& maxs) const
	{
		const float units = static_cast<float>(UNITS_PER_BLOCK);
		const float offset = slot_offset(slot_);
		mins.Init((lo[0] - offset) * units, -hi[2] * units, lo[1] * units + slot_.grid_z);
		maxs.Init((hi[0] - offset) * units, -lo[2] * units, hi[1] * units + slot_.grid_z);
	}

	void WorldCollision::to_mc_box(const Vector& mins, const Vector& maxs, float lo[3], float hi[3]) const
	{
		const float units = static_cast<float>(UNITS_PER_BLOCK);
		const float offset = slot_offset(slot_);
		lo[0] = mins.x / units + offset;
		hi[0] = maxs.x / units + offset;
		lo[1] = (mins.z - slot_.grid_z) / units;
		hi[1] = (maxs.z - slot_.grid_z) / units;
		lo[2] = -maxs.y / units;
		hi[2] = -mins.y / units;
	}

	void WorldCollision::gather(const float lo[3], const float hi[3], ColPrimitives& out)
	{
		Vector mins, maxs;
		to_source_box(lo, hi, mins, maxs);
		add_brushes(mins, maxs, out);
		add_displacements(mins, maxs, out);
		add_static_props(mins, maxs, out);
		add_entities(mins, maxs, out);
	}

	void WorldCollision::add_brushes(const Vector& mins, const Vector& maxs, ColPrimitives& out) const
	{
		CUtlVector<int> brushes;
		enginetrace->GetBrushesInAABB(mins, maxs, &brushes, PLAYER_SOLID_BRUSHES);
		CUtlVector<Vector4D>              planes;
		std::vector<std::array<float, 4>> brush;
		const float                       box_mins[3] = { mins.x, mins.y, mins.z }, box_maxs[3] = { maxs.x, maxs.y, maxs.z };
		for (int i = 0; i < brushes.Count(); ++i) {
			planes.RemoveAll();
			int contents = 0;
			if (!enginetrace->GetBrushInfo(brushes[i], &planes, &contents) || !(contents & PLAYER_SOLID_BRUSHES) || planes.Count() < 4) {
				continue;
			}
			brush.clear();
			for (int p = 0; p < planes.Count(); ++p) {
				brush.push_back({ planes[p].x, planes[p].y, planes[p].z, planes[p].w });
			}
			ColPrimitives::Convex cvx;
			brush_to_convex(brush.data(), brush.size(), box_mins, box_maxs, slot_, cvx);
			out.convexes.push_back(std::move(cvx));
		}
	}

	void WorldCollision::add_displacements(const Vector& mins, const Vector& maxs, ColPrimitives& out) const
	{
		CPhysCollide* collide = enginetrace->GetCollidableFromDisplacementsInAABB(mins, maxs);
		if (!collide) {
			return;
		}
		ICollisionQuery* query = physcollision->CreateQueryModel(collide);
		for (int c = 0; c < query->ConvexCount(); ++c) {
			for (int t = 0; t < query->TriangleCount(c); ++t) {
				Vector verts[3];
				query->GetTriangleVerts(c, t, verts);
				ColPrimitives::Tri tri;
				for (int v = 0; v < 3; ++v) {
					to_mc(verts[v], slot_, tri.v + v * 3);
				}
				tri.flags = 0;
				out.tris.push_back(tri);
			}
		}
		physcollision->DestroyQueryModel(query);
		physcollision->DestroyCollide(collide);
	}

	void WorldCollision::add_static_props(const Vector& mins, const Vector& maxs, ColPrimitives& out)
	{
		CUtlVector<ICollideable*> props;
		staticpropmgr->GetAllStaticPropsInAABB(mins, maxs, &props);
		for (int i = 0; i < props.Count(); ++i) {
			ICollideable* prop = props[i];
			switch (prop->GetSolid()) {
			case SOLID_VPHYSICS: {
				const auto& hulls = model_hulls(prop->GetCollisionModel());
				if (!hulls.empty()) {
					add_hulls(hulls, prop->CollisionToWorldTransform(), 0, out);
					break;
				}
				[[fallthrough]];
			}
			case SOLID_BBOX:
			case SOLID_OBB:
			case SOLID_OBB_YAW:
				add_box(prop->OBBMins(), prop->OBBMaxs(), prop->CollisionToWorldTransform(), out);
				break;
			default:
				break;
			}
		}
	}

	void WorldCollision::add_entities(const Vector& mins, const Vector& maxs, ColPrimitives& out)
	{
		CBaseEntity* list[MAX_ENTITIES];
		const int    count = UTIL_EntitiesInBox(list, MAX_ENTITIES, mins, maxs, 0);
		for (int i = 0; i < count; ++i) {
			if (is_wall(list[i])) {
				add_entity(list[i], out);
			}
		}
	}

	void WorldCollision::add_entity(CBaseEntity* entity, ColPrimitives& out)
	{
		switch (entity->GetSolid()) {
		case SOLID_BSP:
		case SOLID_VPHYSICS: {
			const auto& hulls = model_hulls(entity->GetModel());
			if (!hulls.empty()) {
				add_hulls(hulls, entity->EntityToWorldTransform(), 0, out);
				return;
			}
			break;
		}
		case SOLID_BBOX: {
			matrix3x4_t translation;
			AngleMatrix(vec3_angle, entity->GetAbsOrigin(), translation);
			add_box(entity->WorldAlignMins(), entity->WorldAlignMaxs(), translation, out);
			return;
		}
		default:
			break;
		}
		add_box(entity->CollisionProp()->OBBMins(), entity->CollisionProp()->OBBMaxs(), entity->CollisionProp()->CollisionToWorldTransform(), out);
	}

	const std::vector<WorldCollision::Hull>& WorldCollision::model_hulls(const model_t* model)
	{
		const auto it = hulls_.find(model);
		if (it != hulls_.end()) {
			return it->second;
		}
		auto&       hulls = hulls_[model];
		vcollide_t* collide = model ? modelinfo->GetVCollide(model) : nullptr;
		if (!collide) {
			return hulls;
		}
		for (int s = 0; s < collide->solidCount; ++s) {
			ICollisionQuery* query = physcollision->CreateQueryModel(collide->solids[s]);
			for (int c = 0; c < query->ConvexCount(); ++c) {
				Hull hull;
				for (int t = 0; t < query->TriangleCount(c); ++t) {
					Vector verts[3];
					query->GetTriangleVerts(c, t, verts);
					hull.insert(hull.end(), verts, verts + 3);
				}
				if (!hull.empty()) {
					hulls.push_back(std::move(hull));
				}
			}
			physcollision->DestroyQueryModel(query);
		}
		return hulls;
	}

	void WorldCollision::add_hulls(const std::vector<Hull>& hulls, const matrix3x4_t& to_world, std::uint32_t flags, ColPrimitives& out) const
	{
		std::vector<std::array<float, 3>> corners;
		for (const auto& hull : hulls) {
			corners.clear();
			for (const auto& v : hull) {
				Vector world;
				VectorTransform(v, to_world, world);
				std::array<float, 3> mc;
				to_mc(world, slot_, mc.data());
				corners.push_back(mc);
			}
			ColPrimitives::Convex cvx;
			if (hull_to_convex(corners, flags, cvx)) {
				out.convexes.push_back(std::move(cvx));
			}
		}
	}

	void WorldCollision::add_box(const Vector& mins, const Vector& maxs, const matrix3x4_t& to_world, ColPrimitives& out) const
	{
		// the box as a hull of 12 triangles
		const Vector c[8] = {
			Vector(mins.x, mins.y, mins.z), Vector(maxs.x, mins.y, mins.z), Vector(maxs.x, maxs.y, mins.z), Vector(mins.x, maxs.y, mins.z),
			Vector(mins.x, mins.y, maxs.z), Vector(maxs.x, mins.y, maxs.z), Vector(maxs.x, maxs.y, maxs.z), Vector(mins.x, maxs.y, maxs.z),
		};
		static constexpr int FACES[6][4] = { { 0, 1, 2, 3 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 1, 2, 6, 5 }, { 2, 3, 7, 6 }, { 3, 0, 4, 7 } };
		Hull hull;
		for (const auto& f : FACES) {
			hull.insert(hull.end(), { c[f[0]], c[f[1]], c[f[2]], c[f[0]], c[f[2]], c[f[3]] });
		}
		add_hulls({ hull }, to_world, 0, out);
	}

	void WorldCollision::track_movers(const McVec& player, CollisionStreamer& streamer)
	{
		++frame_;
		// as far as collision is streamed (see hc_collision.cpp), and a region more
		constexpr float REACH_BLOCKS = (CollisionStreamer::RADIUS + 1) * CollisionStreamer::REGION_SIZE;
		const float     lo[3] = { static_cast<float>(player.x) - REACH_BLOCKS, static_cast<float>(player.y) - REACH_BLOCKS, static_cast<float>(player.z) - REACH_BLOCKS };
		const float     hi[3] = { static_cast<float>(player.x) + REACH_BLOCKS, static_cast<float>(player.y) + REACH_BLOCKS, static_cast<float>(player.z) + REACH_BLOCKS };
		Vector          mins, maxs;
		to_source_box(lo, hi, mins, maxs);

		CBaseEntity* list[MAX_ENTITIES];
		const int    count = UTIL_EntitiesInBox(list, MAX_ENTITIES, mins, maxs, 0);
		for (int i = 0; i < count; ++i) {
			CBaseEntity* entity = list[i];
			if (!is_wall(entity)) {
				continue;
			}
			Vector emins, emaxs;
			entity->CollisionProp()->WorldSpaceAABB(&emins, &emaxs);
			const int key = entity->GetRefEHandle().ToInt();
			auto      it = movers_.find(key);
			if (it == movers_.end()) {
				movers_[key] = { entity->GetAbsOrigin(), entity->GetAbsAngles(), emins, emaxs, frame_ };
				continue;
			}
			Mover& mover = it->second;
			if (moved(mover.origin, entity->GetAbsOrigin(), MOVED_UNITS) || turned(mover.angles, entity->GetAbsAngles())) {
				float old_lo[3], old_hi[3], new_lo[3], new_hi[3];
				to_mc_box(mover.mins, mover.maxs, old_lo, old_hi);
				to_mc_box(emins, emaxs, new_lo, new_hi);
				streamer.invalidate(old_lo, old_hi);
				streamer.invalidate(new_lo, new_hi);
				mover.origin = entity->GetAbsOrigin();
				mover.angles = entity->GetAbsAngles();
				mover.mins = emins;
				mover.maxs = emaxs;
			}
			mover.frame = frame_;
		}
		// whatever is gone (broken, removed, out of reach) leaves a hole where it was
		for (auto it = movers_.begin(); it != movers_.end();) {
			if (it->second.frame == frame_) {
				++it;
				continue;
			}
			float old_lo[3], old_hi[3];
			to_mc_box(it->second.mins, it->second.maxs, old_lo, old_hi);
			streamer.invalidate(old_lo, old_hi);
			it = movers_.erase(it);
		}
	}
}

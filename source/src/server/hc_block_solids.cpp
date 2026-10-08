// server.dll: minecraft's blocks as half-life collision (see hc_block_solids.h).

#include "cbase.h"
#include "debugoverlay_shared.h"
#include "physics_shared.h"
#include "vphysics_interface.h"

#include "tier0/valve_minmax_off.h"
#include <cmath>
#include <cstring>
#include <vector>

#include "halfcraft_protocol.h"
#include "core/hc_log.h"
#include "core/hc_module.h"
#include "core/hc_units.h"
#include "server/hc_block_solids.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
	constexpr int   SECTION_BLOCKS = 16;
	constexpr float KEEP_SLOT_BLOCKS = 600.0f;  // sections further than this from the map's slot belong to another map
	constexpr float BULLET_DEPTH = 1.0f;        // units behind the face a bullet hit: inside its block for sure

	/// solid blocks merged into boxes (minecraft block coords within the section, inclusive).
	struct BlockBox
	{
		int x0, y0, z0, x1, y1, z1;
	};

	std::vector<BlockBox> merge_blocks(const std::uint8_t bits[512])
	{
		bool left[SECTION_BLOCKS][SECTION_BLOCKS][SECTION_BLOCKS];  // [y][z][x]
		for (int i = 0; i < SECTION_BLOCKS * SECTION_BLOCKS * SECTION_BLOCKS; ++i) {
			left[i >> 8][(i >> 4) & 15][i & 15] = (bits[i >> 3] >> (i & 7)) & 1;
		}
		std::vector<BlockBox> boxes;
		for (int y = 0; y < SECTION_BLOCKS; ++y) {
			for (int z = 0; z < SECTION_BLOCKS; ++z) {
				for (int x = 0; x < SECTION_BLOCKS; ++x) {
					if (!left[y][z][x]) {
						continue;
					}
					// greedy: as far as it goes along x, then whole rows along z, then whole layers up
					int x1 = x;
					while (x1 + 1 < SECTION_BLOCKS && left[y][z][x1 + 1]) {
						++x1;
					}
					int z1 = z;
					for (bool grow = true; grow && z1 + 1 < SECTION_BLOCKS;) {
						for (int xx = x; xx <= x1 && grow; ++xx) {
							grow = left[y][z1 + 1][xx];
						}
						z1 += grow ? 1 : 0;
					}
					int y1 = y;
					for (bool grow = true; grow && y1 + 1 < SECTION_BLOCKS;) {
						for (int zz = z; zz <= z1 && grow; ++zz) {
							for (int xx = x; xx <= x1 && grow; ++xx) {
								grow = left[y1 + 1][zz][xx];
							}
						}
						y1 += grow ? 1 : 0;
					}
					for (int yy = y; yy <= y1; ++yy) {
						for (int zz = z; zz <= z1; ++zz) {
							for (int xx = x; xx <= x1; ++xx) {
								left[yy][zz][xx] = false;
							}
						}
					}
					boxes.push_back({ x, y, z, x1, y1, z1 });
				}
			}
		}
		return boxes;
	}

	// the server deletes physics objects at the end of the frame (CleanupDeleteList), and the object
	// still reads its collision model then: a section's old model is freed a frame after its object
	// went, never at once
	struct DeadCollide
	{
		CPhysCollide* collide;
		int           frame;
	};
	std::vector<DeadCollide> g_dead_collides;

	void free_dead_collides(bool all)
	{
		auto it = g_dead_collides.begin();
		while (it != g_dead_collides.end()) {
			if (all || it->frame < gpGlobals->framecount) {
				physcollision->DestroyCollide(it->collide);
				it = g_dead_collides.erase(it);
			} else {
				++it;
			}
		}
	}

	/// a half-life bullet went into one of minecraft's blocks: proto::kInBulletHit.
	/// @param inside - a point inside the block
	void push_bullet_hit(const Vector& inside, halfcraft::MapSlot slot)
	{
		static halfcraft::PushInputFn push_input = nullptr;
		if (!push_input) {
			push_input = reinterpret_cast<halfcraft::PushInputFn>(halfcraft::find_export("client.dll", halfcraft::HC_PUSH_INPUT_EXPORT));
			if (!push_input) {
				return;
			}
		}
		const halfcraft::McVec block = halfcraft::source_to_mc(inside.Base(), slot);
		push_input(halfcraft::proto::kInBulletHit, 0, static_cast<int>(std::floor(block.x)), static_cast<int>(std::floor(block.y)),
			static_cast<int>(std::floor(block.z)));
	}
}

//-----------------------------------------------------------------------------
// one section's solid blocks: invisible, never networked, never saved (minecraft keeps the blocks;
// they come back from it after a load).
//-----------------------------------------------------------------------------
class CHalfCraftBlocks : public CBaseEntity
{
public:
	DECLARE_CLASS(CHalfCraftBlocks, CBaseEntity);

	void Spawn(void) override
	{
		BaseClass::Spawn();
		// the section's box puts it in the spatial partition; the custom tests send every ray and
		// swept box that reaches it to TestCollision, against the blocks themselves
		SetSolid(SOLID_BBOX);
		AddSolidFlags(FSOLID_CUSTOMRAYTEST | FSOLID_CUSTOMBOXTEST);
		SetMoveType(MOVETYPE_NONE);
		SetCollisionGroup(COLLISION_GROUP_NONE);
		AddEffects(EF_NODRAW);
	}

	int UpdateTransmitState(void) override { return SetTransmitState(FL_EDICT_DONTSEND); }
	int ObjectCaps(void) override { return BaseClass::ObjectCaps() | FCAP_DONT_SAVE; }

	void UpdateOnRemove(void) override
	{
		ReleaseCollide();
		BaseClass::UpdateOnRemove();
	}

	bool TestCollision(const Ray_t& ray, unsigned int mask, trace_t& trace) override
	{
		if (!m_pCollide || !(mask & CONTENTS_SOLID))
			return false;
		UTIL_ClearTrace(trace);
		physcollision->TraceBox(ray, m_pCollide, GetAbsOrigin(), vec3_angle, &trace);
		if (!trace.DidHit())
			return false;
		trace.m_pEnt = this;
		trace.contents = CONTENTS_SOLID;
		return true;
	}

	/// a bullet into the blocks: minecraft breaks the one it went into if a bullet would (glass, panes, ice).
	void TraceAttack(const CTakeDamageInfo& info, const Vector& dir, trace_t* trace, CDmgAccumulator* accumulator) override
	{
		if (trace && (info.GetDamageType() & (DMG_BULLET | DMG_BUCKSHOT))) {
			// the blocks are whole cubes here, whatever their shape in minecraft
			push_bullet_hit(trace->endpos - trace->plane.normal * BULLET_DEPTH, m_slot);
		}
		BaseClass::TraceAttack(info, dir, trace, accumulator);
	}

	/// where the map sits in minecraft, for the blocks' coordinates.
	void SetSlot(halfcraft::MapSlot slot) { m_slot = slot; }

	/// the section's blocks, as boxes in this entity's space (units).
	void SetBoxes(const std::vector<Vector>& mins, const std::vector<Vector>& maxs, const Vector& bounds_mins, const Vector& bounds_maxs)
	{
		ReleaseCollide();
		std::vector<CPhysConvex*> convexes;
		convexes.reserve(mins.size());
		for (size_t i = 0; i < mins.size(); ++i) {
			if (CPhysConvex* convex = physcollision->BBoxToConvex(mins[i], maxs[i]))
				convexes.push_back(convex);
		}
		if (convexes.empty())
			return;
		m_pCollide = physcollision->ConvertConvexToCollide(convexes.data(), static_cast<int>(convexes.size()));
		SetCollisionBounds(bounds_mins, bounds_maxs);
		if (m_pCollide) {
			IPhysicsObject* pObject = PhysModelCreateCustom(this, m_pCollide, GetAbsOrigin(), vec3_angle, halfcraft::BLOCKS_CLASSNAME, true);
			VPhysicsSetObject(pObject);
		}
	}

private:
	void ReleaseCollide(void)
	{
		VPhysicsDestroyObject();
		if (m_pCollide) {
			g_dead_collides.push_back({ m_pCollide, gpGlobals->framecount });
			m_pCollide = nullptr;
		}
	}

	CPhysCollide*      m_pCollide = nullptr;
	halfcraft::MapSlot m_slot;
};

LINK_ENTITY_TO_CLASS(halfcraft_blocks, CHalfCraftBlocks);

namespace halfcraft
{
	void BlockSolids::reset(MapSlot slot)
	{
		free_dead_collides(true);  // a new level: the old one's physics objects are gone
		slot_ = slot;
		version_ = 0;  // ask for everything again
		entities_.clear();
		dropped_ = false;
	}

	namespace
	{
		ConVar hc_debug_blocks("hc_debug_blocks", "0", 0, "halfcraft: outline the collision of minecraft's blocks");
		ConVar hc_debug_drop("hc_debug_drop", "0", 0, "halfcraft: drop a watermelon onto minecraft's blocks once they're in");

		constexpr char DROP_MODEL[] = "models/props_junk/watermelon01.mdl";
	}

	void BlockSolids::debug_draw()
	{
		const bool outline = hc_debug_blocks.GetBool();
		const bool drop = hc_debug_drop.GetBool() && !dropped_ && gpGlobals->curtime > 3.0f;
		if (!outline && !drop && drop_time_ <= 0.0f) {
			return;
		}
		for (const auto& entry : entities_) {
			CBaseEntity* entity = entry.second.Get();
			if (!entity) {
				continue;
			}
			Vector mins, maxs;
			entity->CollisionProp()->WorldSpaceAABB(&mins, &maxs);
			if (outline) {
				NDebugOverlay::Box(vec3_origin, mins, maxs, 255, 160, 0, 40, 0.2f);
			}
			if (drop && !dropped_) {
				// straight down through every block column of the section: do plain engine traces stop
				// on the blocks? the melon goes above the first column that does.
				Vector above((mins.x + maxs.x) * 0.5f, (mins.y + maxs.y) * 0.5f, maxs.z + 64.0f);
				int    columns = 0, hits = 0;
				float  hit_z = 0.0f;
				for (float x = mins.x + 20.0f; x < maxs.x; x += 40.0f) {
					for (float y = mins.y + 20.0f; y < maxs.y; y += 40.0f) {
						trace_t tr;
						UTIL_TraceLine(Vector(x, y, maxs.z + 64.0f), Vector(x, y, mins.z - 64.0f), MASK_SOLID, nullptr, COLLISION_GROUP_NONE, &tr);
						++columns;
						if (tr.m_pEnt && FClassnameIs(tr.m_pEnt, BLOCKS_CLASSNAME)) {
							if (hits++ == 0) {
								above.Init(x, y, tr.endpos.z + 16.0f);  // just above the top: no bounce off a one-block column
								hit_z = tr.endpos.z;
							}
						}
					}
				}
				log_info("traces down through the blocks: %d of %d columns stopped on them (first at z %.0f; blocks span z %.0f..%.0f)", hits, columns, hit_z,
					mins.z, maxs.z);

				dropped_ = true;
				CBaseEntity::PrecacheModel(DROP_MODEL);
				if (CBaseEntity* melon = CreateEntityByName("prop_physics")) {
					melon->KeyValue("model", DROP_MODEL);
					melon->SetAbsOrigin(above);
					DispatchSpawn(melon);
					melon_ = melon;
					drop_time_ = gpGlobals->curtime;
					log_info("dropped a watermelon at (%.0f %.0f %.0f)", above.x, above.y, above.z);
				}
			}
		}
		if (melon_.Get() && drop_time_ > 0.0f && !melon_early_ && gpGlobals->curtime > drop_time_ + 0.3f) {
			const Vector& at = melon_->GetAbsOrigin();
			log_info("the watermelon after 0.3 s: (%.0f %.0f %.0f)", at.x, at.y, at.z);
			melon_early_ = true;
		}
		if (melon_.Get() && drop_time_ > 0.0f && gpGlobals->curtime > drop_time_ + 3.0f) {
			const Vector& at = melon_->GetAbsOrigin();
			log_info("the watermelon came to rest at (%.0f %.0f %.0f)", at.x, at.y, at.z);
			drop_time_ = 0.0f;
		}
	}

	void BlockSolids::update()
	{
		free_dead_collides(false);
		debug_draw();
		if (!solids_since_) {
			solids_since_ = reinterpret_cast<SolidsSinceFn>(find_export("client.dll", HC_SOLIDS_EXPORT));
			if (!solids_since_) {
				return;
			}
		}
		std::uint32_t now = version_;
		const int     changed = solids_since_(version_, nullptr, 0, &now);
		if (changed <= 0) {
			version_ = now;
			return;
		}
		changes_.resize(static_cast<std::size_t>(changed));
		const int copied = solids_since_(version_, changes_.data(), changed, &now);
		for (int i = 0; i < copied && i < changed; ++i) {
			apply(changes_[i]);
		}
		version_ = now;
		log_info("block collision: %d section(s) changed, %d in this map", copied, static_cast<int>(entities_.size()));
	}

	void BlockSolids::apply(const SolidSection& section)
	{
		const Key   key{ section.sx, section.sy, section.sz };
		const float centre = static_cast<float>(slot_.x_blocks());
		const bool  this_map = std::fabs(static_cast<float>(section.sx * SECTION_BLOCKS) - centre) <= KEEP_SLOT_BLOCKS;
		auto        it = entities_.find(key);
		if (section.count == 0 || !this_map) {
			if (it != entities_.end()) {
				if (CBaseEntity* entity = it->second.Get()) {
					UTIL_Remove(entity);
				}
				entities_.erase(it);
			}
			return;
		}

		// the entity sits at the section's source minimum corner (minecraft's max z)
		const float units = static_cast<float>(UNITS_PER_BLOCK);
		float       corner[3];
		mc_to_source(section.sx * SECTION_BLOCKS, section.sy * SECTION_BLOCKS, (section.sz + 1) * SECTION_BLOCKS, slot_, corner);
		const Vector origin(corner[0], corner[1], corner[2]);

		std::vector<Vector> mins, maxs;
		Vector              lo(FLT_MAX, FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (const auto& box : merge_blocks(section.bits)) {
			const Vector box_min(box.x0 * units, (SECTION_BLOCKS - box.z1 - 1) * units, box.y0 * units);
			const Vector box_max((box.x1 + 1) * units, (SECTION_BLOCKS - box.z0) * units, (box.y1 + 1) * units);
			mins.push_back(box_min);
			maxs.push_back(box_max);
			lo = lo.Min(box_min);
			hi = hi.Max(box_max);
		}
		if (mins.empty()) {
			return;
		}

		CHalfCraftBlocks* entity = it != entities_.end() ? static_cast<CHalfCraftBlocks*>(it->second.Get()) : nullptr;
		if (!entity) {
			entity = static_cast<CHalfCraftBlocks*>(CreateEntityByName(BLOCKS_CLASSNAME));
			if (!entity) {
				log_error("couldn't create %s", BLOCKS_CLASSNAME);
				return;
			}
			entity->SetAbsOrigin(origin);
			entity->SetSlot(slot_);
			DispatchSpawn(entity);
			entities_[key] = entity;
		}
		entity->SetBoxes(mins, maxs, lo, hi);
	}
}

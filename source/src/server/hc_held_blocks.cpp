// server.dll: half-life's gravity gun tears minecraft's blocks out (see hc_held_blocks.h).

#include "cbase.h"
#include "physics_shared.h"
#include "player_pickup.h"
#include "vcollide_parse.h"
#include "vphysics_interface.h"

#include "tier0/valve_minmax_off.h"
#include <cmath>
#include <cstdint>

#include "core/hc_log.h"
#include "server/hc_block_solids.h"
#include "server/hc_held_blocks.h"
#include "shared/hc_bridge.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
	using halfcraft::proto::kMaxHeldBlocks;

	constexpr char PROP_CLASSNAME[] = "halfcraft_block_prop";
	// a hair under a block: it fits back into its cell, and the gun's own traces don't start in a neighbour
	constexpr float HALF = static_cast<float>(halfcraft::UNITS_PER_BLOCK) * 0.5f - 0.5f;
	constexpr float TAKE_INTERVAL = 0.25f;  // the gun asks every frame it looks at a wall: one block at a time
	constexpr float REST_SPEED = 4.0f;      // units a second: slower than this, it's at rest
	constexpr float REST_SPIN = 10.0f;      // degrees a second
	constexpr float REST_SECONDS = 0.4f;    // at rest that long, it lands
	constexpr float NEWS_TIMEOUT = 3.0f;    // minecraft answers (proto::kEvHeldBlock) sooner than this
	constexpr float PLACE_TIMEOUT = 1.0f;   // a block minecraft put back reaches source's collision sooner than this
	constexpr float WALL_TIMEOUT = 1.5f;    // the wall a block came out of has lost it by then

	enum class Phase
	{
		kTaking,   // minecraft hasn't said yet that it took the block out
		kHeld,     // it did: the cube is the block until it comes to rest
		kLanding,  // at rest and snapped to its cell: minecraft puts the block there
		kPlacing,  // minecraft did: the cube goes once source's collision has the block
		kDone,     // minecraft is done with it (placed, dropped, refused): it just goes
	};

	/// the half-life surface and weight of each proto::HeldMaterial: light enough for the gravity gun,
	/// heavy enough to hurt
	struct Surface
	{
		const char* name;
		float       kg;
	};
	constexpr Surface SURFACES[] = {
		{ "concrete_block", 30.0f },  // kMatStone
		{ "Wood_Solid", 15.0f },      // kMatWood
		{ "glass", 12.0f },           // kMatGlass
		{ "dirt", 20.0f },            // kMatDirt
		{ "gravel", 25.0f },          // kMatGravel
		{ "sand", 25.0f },            // kMatSand
		{ "grass", 15.0f },           // kMatGrass
		{ "Metal_Box", 50.0f },       // kMatMetal
		{ "carpet", 5.0f },           // kMatWool
		{ "snow", 8.0f },             // kMatSnow
	};

	/// traces that see minecraft's blocks (halfcraft_blocks) and nothing else, half-life's world included.
	class BlocksOnlyFilter : public CTraceFilter
	{
	public:
		bool ShouldHitEntity(IHandleEntity* handle, int) override
		{
			CBaseEntity* entity = EntityFromEntityHandle(handle);
			return entity && FClassnameIs(entity, halfcraft::BLOCKS_CLASSNAME);
		}
	};
}

//-----------------------------------------------------------------------------
// a block the gravity gun tore out: a physics cube half-life throws around. no model of its own: client.dll
// draws the block's faces on it (proto::kWeHeldBlock by its held slot, C_HalfCraftBlockProp in
// hc_things.cpp), so it's networked to it and interpolated there. never saved (hc_held_blocks.h).
//-----------------------------------------------------------------------------
class CHalfCraftBlockProp : public CBaseEntity, public CDefaultPlayerPickupVPhysics
{
public:
	DECLARE_CLASS(CHalfCraftBlockProp, CBaseEntity);
	DECLARE_SERVERCLASS();

	void Spawn(void) override
	{
		BaseClass::Spawn();
		const Vector half(HALF, HALF, HALF);
		m_pCollide = physcollision->BBoxToCollide(-half, half);
		SetSolid(SOLID_VPHYSICS);
		// no model to trace against: traces (the gravity gun finding it again, bullets) come to TestCollision
		AddSolidFlags(FSOLID_CUSTOMRAYTEST | FSOLID_CUSTOMBOXTEST);
		SetMoveType(MOVETYPE_VPHYSICS);
		SetCollisionBounds(-half, half);
		if (m_pCollide) {
			// the default box solid never turns (its inertia balances a cube on an edge): a real box's,
			// worked out from its shape
			solid_t solid;
			PhysGetDefaultAABBSolid(solid);
			solid.params.mass = SURFACES[0].kg;
			solid.params.inertia = 1.0f;
			solid.params.volume = physcollision->CollideVolume(m_pCollide);
			if (IPhysicsObject* object = PhysModelCreateCustom(this, m_pCollide, GetAbsOrigin(), GetAbsAngles(), PROP_CLASSNAME, false, &solid)) {
				VPhysicsSetObject(object);
				SetMaterial(halfcraft::proto::kMatStone);
				object->Wake();
			}
		}
		// nothing else (no model, no effects change) would ever ask whether to network it
		DispatchUpdateTransmitState();
	}

	int UpdateTransmitState(void) override { return SetTransmitState(FL_EDICT_ALWAYS); }
	int ObjectCaps(void) override { return BaseClass::ObjectCaps() | FCAP_DONT_SAVE; }

	bool TestCollision(const Ray_t& ray, unsigned int mask, trace_t& trace) override
	{
		if (!m_pCollide || !(mask & CONTENTS_SOLID))
			return false;
		UTIL_ClearTrace(trace);
		physcollision->TraceBox(ray, m_pCollide, GetAbsOrigin(), GetAbsAngles(), &trace);
		if (!trace.DidHit())
			return false;
		trace.m_pEnt = this;
		trace.contents = CONTENTS_SOLID;
		return true;
	}

	// carried square to the player, the way a block sits in a wall
	bool   HasPreferredCarryAnglesForPlayer(CBasePlayer*) override { return true; }
	QAngle PreferredCarryAngles(void) override { return vec3_angle; }

	void UpdateOnRemove(void) override;

	/// what minecraft says the block is made of (proto::HeldMaterial): it sounds, slides and weighs like that.
	void SetMaterial(std::uint32_t material)
	{
		const Surface&  surface = SURFACES[material < ARRAYSIZE(SURFACES) ? material : 0];
		IPhysicsObject* object = VPhysicsGetObject();
		if (!object)
			return;
		int index = physprops->GetSurfaceIndex(surface.name);
		if (index < 0)
			index = physprops->GetSurfaceIndex("concrete");
		object->SetMaterialIndex(index);
		object->SetMass(surface.kg);
	}

	/// the cube's centre as a minecraft block cell.
	void CellOf(int cell[3]) const
	{
		const halfcraft::McVec mc = halfcraft::source_to_mc(WorldSpaceCenter().Base(), m_slot);
		cell[0] = static_cast<int>(std::floor(mc.x));
		cell[1] = static_cast<int>(std::floor(mc.y));
		cell[2] = static_cast<int>(std::floor(mc.z));
	}

	CNetworkVar(int, m_iHeldSlot);
	halfcraft::MapSlot m_slot;
	Phase              m_phase = Phase::kTaking;
	int                m_taken[3] = {};    // the cell it came out of
	int                m_cell[3] = {};     // the cell it landed in
	float              m_flTakenAt = 0.0f;
	float              m_flRestSince = -1.0f;
	float              m_flDeadline = 0.0f;
	EHANDLE            m_hWall;            // the blocks it came out of, which it passes through until they lose it

private:
	CPhysCollide* m_pCollide = nullptr;
};

LINK_ENTITY_TO_CLASS(halfcraft_block_prop, CHalfCraftBlockProp);

IMPLEMENT_SERVERCLASS_ST(CHalfCraftBlockProp, DT_HalfCraftBlockProp)
SendPropInt(SENDINFO(m_iHeldSlot), 6, SPROP_UNSIGNED),
	END_SEND_TABLE()

		namespace halfcraft
{
	namespace
	{
		CHandle<CHalfCraftBlockProp> g_props[kMaxHeldBlocks];
		int                          g_next = 0;
		float                        g_last_take = -1.0f;
		MapSlot                      g_slot;

		void push(proto::InputType type, int slot, const int cell[3])
		{
			if (PushInputFn push_input = client_push_input()) {
				push_input(type, slot, cell[0], cell[1], cell[2]);
			}
		}

		Vector cell_centre(const int cell[3], MapSlot slot)
		{
			float centre[3];
			mc_to_source(cell[0] + 0.5, cell[1] + 0.5, cell[2] + 0.5, slot, centre);
			return Vector(centre[0], centre[1], centre[2]);
		}

		/// whether source's collision has a minecraft block in the cell.
		bool cell_solid(const int cell[3], MapSlot slot)
		{
			const Vector     centre = cell_centre(cell, slot);
			const Vector     up(0.0f, 0.0f, 1.0f);
			BlocksOnlyFilter filter;
			trace_t          tr;
			UTIL_TraceLine(centre + up, centre - up, MASK_SOLID, &filter, &tr);
			return tr.DidHit();
		}

		/// the cube's done (minecraft has or keeps the block): it goes without a word to minecraft.
		void finish(CHalfCraftBlockProp* prop)
		{
			prop->m_phase = Phase::kDone;
			UTIL_Remove(prop);
		}

		/// at rest: the cube snaps into the cell its centre is in, holds still there, and minecraft puts
		/// the block back.
		void land(CHalfCraftBlockProp* prop)
		{
			prop->CellOf(prop->m_cell);
			const Vector centre = cell_centre(prop->m_cell, prop->m_slot);
			prop->Teleport(&centre, &vec3_angle, &vec3_origin);
			if (IPhysicsObject* object = prop->VPhysicsGetObject()) {
				object->EnableMotion(false);
			}
			prop->m_phase = Phase::kLanding;
			prop->m_flDeadline = gpGlobals->curtime + NEWS_TIMEOUT;
			push(proto::kInHeldBlockLanded, prop->m_iHeldSlot, prop->m_cell);
			log_info("gravity gun: held slot %d came to rest at block %d %d %d", prop->m_iHeldSlot.Get(), prop->m_cell[0], prop->m_cell[1], prop->m_cell[2]);
		}

		/// a held cube's turn: lands once at rest, goes once minecraft is done.
		void update_prop(CHalfCraftBlockProp* prop)
		{
			const float now = gpGlobals->curtime;
			if (CBaseEntity* wall = prop->m_hWall.Get()) {
				if (!cell_solid(prop->m_taken, prop->m_slot) || now - prop->m_flTakenAt > WALL_TIMEOUT) {
					PhysEnableEntityCollisions(prop, wall);
					prop->m_hWall = nullptr;
				}
			}
			IPhysicsObject* object = prop->VPhysicsGetObject();
			switch (prop->m_phase) {
			case Phase::kTaking:
				if (now - prop->m_flTakenAt > NEWS_TIMEOUT) {
					log_warning("gravity gun: minecraft never answered for held slot %d; the cube goes", prop->m_iHeldSlot.Get());
					finish(prop);
				}
				break;
			case Phase::kHeld: {
				if (!object || (object->GetGameFlags() & FVPHYSICS_PLAYER_HELD) || !object->IsMotionEnabled()) {
					prop->m_flRestSince = -1.0f;
					break;
				}
				Vector         velocity;
				AngularImpulse spin;
				object->GetVelocity(&velocity, &spin);
				const bool resting = object->IsAsleep() || (velocity.LengthSqr() < REST_SPEED * REST_SPEED && spin.LengthSqr() < REST_SPIN * REST_SPIN);
				if (!resting) {
					prop->m_flRestSince = -1.0f;
				} else if (prop->m_flRestSince < 0.0f) {
					prop->m_flRestSince = now;
				} else if (now - prop->m_flRestSince >= REST_SECONDS) {
					land(prop);
				}
				break;
			}
			case Phase::kLanding:
				if (now > prop->m_flDeadline) {
					log_warning("gravity gun: minecraft never answered where held slot %d landed; the cube goes", prop->m_iHeldSlot.Get());
					finish(prop);
				}
				break;
			case Phase::kPlacing:
				if (cell_solid(prop->m_cell, prop->m_slot) || now > prop->m_flDeadline) {
					finish(prop);
				}
				break;
			case Phase::kDone:
				break;
			}
		}
	}

	CBaseEntity* take_block(const int cell[3], const Vector& velocity, CBaseEntity* wall)
	{
		const float now = gpGlobals->curtime;
		if (g_last_take >= 0.0f && now >= g_last_take && now - g_last_take < TAKE_INTERVAL) {
			return nullptr;
		}
		if (!client_push_input()) {
			return nullptr;
		}
		// a free held slot, or the oldest one's cube goes (leaving its block as an item)
		int slot = g_next;
		for (int i = 0; i < static_cast<int>(kMaxHeldBlocks); ++i) {
			const int candidate = (g_next + i) % kMaxHeldBlocks;
			if (!g_props[candidate].Get()) {
				slot = candidate;
				break;
			}
		}
		g_next = (slot + 1) % kMaxHeldBlocks;
		if (CHalfCraftBlockProp* old = g_props[slot].Get()) {
			UTIL_Remove(old);
		}

		auto* prop = static_cast<CHalfCraftBlockProp*>(CreateEntityByName(PROP_CLASSNAME));
		if (!prop) {
			log_error("couldn't create %s", PROP_CLASSNAME);
			return nullptr;
		}
		prop->m_iHeldSlot = slot;
		prop->m_slot = g_slot;
		prop->m_taken[0] = cell[0];
		prop->m_taken[1] = cell[1];
		prop->m_taken[2] = cell[2];
		prop->m_flTakenAt = now;
		prop->SetAbsOrigin(cell_centre(cell, g_slot));
		DispatchSpawn(prop);
		prop->Activate();
		// the block is in the wall's collision until minecraft's word that it's gone comes back round
		if (wall) {
			PhysDisableEntityCollisions(prop, wall);
			prop->m_hWall = wall;
		}
		if (velocity != vec3_origin) {
			if (IPhysicsObject* object = prop->VPhysicsGetObject()) {
				AngularImpulse spin(RandomFloat(-300.0f, 300.0f), RandomFloat(-300.0f, 300.0f), RandomFloat(-300.0f, 300.0f));
				object->SetVelocity(&velocity, &spin);
			}
		}
		g_props[slot] = prop;
		g_last_take = now;
		push(proto::kInTakeBlock, slot, cell);
		log_info("gravity gun: block %d %d %d torn out into held slot %d%s", cell[0], cell[1], cell[2], slot, velocity != vec3_origin ? " (punted)" : "");
		return prop;
	}

	void HeldBlocks::reset(MapSlot slot)
	{
		g_slot = slot;
		for (auto& prop : g_props) {
			prop = nullptr;  // they went with the last level (each told minecraft so)
		}
	}

	void HeldBlocks::update()
	{
		for (auto& handle : g_props) {
			if (CHalfCraftBlockProp* prop = handle.Get()) {
				update_prop(prop);
			}
		}
	}

	void HeldBlocks::on_news(const proto::McEvent& event)
	{
		if (event.actorId >= kMaxHeldBlocks) {
			return;
		}
		CHalfCraftBlockProp* prop = g_props[event.actorId].Get();
		if (!prop) {
			return;
		}
		switch (event.flags) {
		case proto::kHeldTaken:
			if (prop->m_phase == Phase::kTaking) {
				prop->SetMaterial(event.weapon);
				prop->m_phase = Phase::kHeld;
			}
			break;
		case proto::kHeldRefused:
			log_info("gravity gun: minecraft kept the block of held slot %u", event.actorId);
			finish(prop);
			break;
		case proto::kHeldPlaced:
			if (prop->m_phase == Phase::kLanding) {
				prop->m_phase = Phase::kPlacing;
				prop->m_flDeadline = gpGlobals->curtime + PLACE_TIMEOUT;
			}
			break;
		case proto::kHeldDropped:
			finish(prop);
			break;
		default:
			break;
		}
	}
}

void CHalfCraftBlockProp::UpdateOnRemove(void)
{
	// gone before minecraft was done with it (a level change, dissolved, the oldest of too many): the
	// block lies where the cube was
	if (m_phase != Phase::kDone) {
		int cell[3];
		CellOf(cell);
		halfcraft::push(halfcraft::proto::kInHeldBlockLost, m_iHeldSlot, cell);
	}
	const int slot = m_iHeldSlot;
	if (slot >= 0 && slot < static_cast<int>(kMaxHeldBlocks) && halfcraft::g_props[slot].Get() == this) {
		halfcraft::g_props[slot] = nullptr;
	}
	VPhysicsDestroyObject();
	halfcraft::free_collide_later(m_pCollide);
	m_pCollide = nullptr;
	BaseClass::UpdateOnRemove();
}

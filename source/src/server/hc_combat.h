#pragma once

// server.dll: fighting across the two games (port of SkyCraft's Combat.cpp).
//
//   actors     npcs (and breakable props) near the player go to minecraft every frame; it mirrors
//              them as invisible stand-ins its weapons hit, and won't build blocks into them
//   hits       minecraft's hits on a stand-in come back as half-life damage on the real thing, the
//              way a weapon would deal it (blood, flinches, death, burning, knockback)
//   hurts      half-life's damage to the player goes to minecraft's health (CBasePlayer hook), so
//              minecraft's armour, shields and death decide
//   explosions tnt and creepers hurt npcs and throw props around in half-life too (hc_blast.h)
//   arrows     minecraft's arrows that stick in npcs stay drawn on them (client.dll pins them)

#include <vector>

#include "core/hc_link.h"
#include "core/hc_units.h"

class CBasePlayer;
class CBaseEntity;

namespace halfcraft
{
	class Combat
	{
	public:
		/// a map loaded.
		/// @param slot - where it sits in minecraft (the player's hurts need it before the first update)
		void reset(MapSlot slot);
		/// once per frame.
		/// @param slot - where the map sits in minecraft (minecraft <-> source placement)
		/// @param minecraft_playing - minecraft's player is in its world and alive (otherwise nothing is
		///   mirrored). also while source has the player (a ladder, a ride, a vehicle): only the
		///   movement is source's then
		void update(Link& link, CBasePlayer* player, MapSlot slot, bool minecraft_playing);

	private:
		void write_actors(Link& link, CBasePlayer* player, MapSlot slot);
		void apply_hit(CBasePlayer* player, const proto::McEvent& event);
		void apply_explosion(CBasePlayer* player, const proto::McEvent& event, MapSlot slot);
		/// hands a minecraft arrow that stuck in an npc to client.dll, which draws it on the npc.
		void stick_arrow(const proto::McEvent& event, MapSlot slot);

		std::vector<proto::ActorRecord> records_;
		bool                            actors_sent_ = false;
	};

	/// the id minecraft knows a host actor by (proto::ActorRecord::id, and the attacker of its hurts).
	std::uint32_t host_actor_id(CBaseEntity* entity);
	/// how minecraft should take a half-life hit of this damage type (DMG_*).
	/// @return proto::HurtKind
	int hurt_kind(int damage_type);
}

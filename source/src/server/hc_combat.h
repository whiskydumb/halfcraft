#pragma once

// server.dll: fighting across the two games (port of SkyCraft's Combat.cpp).
//
//   actors     npcs (and breakable props) near the player go to minecraft every frame; it mirrors
//              them as invisible stand-ins its weapons hit, and won't build blocks into them
//   hits       minecraft's hits on a stand-in come back as half-life damage on the real thing, the
//              way a weapon would deal it (blood, flinches, death, burning, knockback)
//   hurts      half-life's damage to the player goes to minecraft's health (CBasePlayer hook), so
//              minecraft's armour, shields and death decide
//   explosions tnt and creepers hurt npcs and throw props around in half-life too
//   arrows     minecraft's arrows that stick in npcs stay drawn on them (client.dll pins them)

#include <vector>

#include "core/hc_link.h"
#include "core/hc_units.h"

class CBasePlayer;

namespace halfcraft
{
	class Combat
	{
	public:
		/// once per frame.
		/// @param slot - where the map sits in minecraft (minecraft <-> source placement)
		/// @param puppeted - minecraft drives the player (otherwise nothing is mirrored)
		void update(Link& link, CBasePlayer* player, MapSlot slot, bool puppeted);

	private:
		void write_actors(Link& link, CBasePlayer* player, MapSlot slot);
		void apply_hit(CBasePlayer* player, const proto::McEvent& event);
		void apply_explosion(CBasePlayer* player, const proto::McEvent& event, MapSlot slot);
		/// hands a minecraft arrow that stuck in an npc to client.dll, which draws it on the npc.
		void stick_arrow(const proto::McEvent& event, MapSlot slot);

		std::vector<proto::ActorRecord> records_;
		bool                            actors_sent_ = false;
	};
}

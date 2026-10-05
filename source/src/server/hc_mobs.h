#pragma once

// server.dll: minecraft's mobs among half-life's characters (protocol MobTable).
//
// every monster and pet minecraft lists near its player gets an invisible stand-in, halfcraft_mob: an
// npc_bullseye half-life's characters see through their own relationship tables. monsters are
// CLASS_ZOMBIE (the combine and the rebels hate them, half-life's zombies and headcrabs leave them
// alone), the player's pets CLASS_PLAYER_ALLY. what half-life does to a stand-in goes to minecraft as
// proto::kInHurtMob, blamed on the attacking actor; minecraft decides what it does to its mob, so a
// stand-in never loses health or dies here. stand-ins are never saved: minecraft lists them again.

#include <cstddef>
#include <cstdint>

#include "core/hc_link.h"
#include "core/hc_units.h"

class CBaseEntity;

namespace halfcraft
{
	class Mobs
	{
	public:
		/// once per frame: the stand-ins follow minecraft's table (made, moved, resized, removed).
		/// @param slot - where the map sits in minecraft
		void update(Link& link, MapSlot slot);
		/// the level is going away, and its stand-ins with it.
		void reset();

	private:
		proto::MobTable table_{};
		std::uint32_t   frame_ = 0;
		std::size_t     logged_ = 0;
	};

	/// the stand-in of one of minecraft's mobs, by its minecraft entity id; nullptr when it has none.
	CBaseEntity* mob_stand_in(std::uint32_t mc_id);
}

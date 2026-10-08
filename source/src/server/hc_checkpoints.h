#pragma once

// server.dll: half-life's saves roll minecraft back too. every save carries a checkpoint id (in a
// logical entity saved with the level); saving tells minecraft to keep its world and player as they
// are under that id, and loading a save tells it to go back to them. level transitions restore the
// levels' entities as well but aren't loads, so they leave minecraft's rollback alone.
//
// a new game from the menu starts a playthrough (proto::kInMapEntered): minecraft clears the builds of
// each map the playthrough enters for the first time, the new game's own map and every one a level
// transition brings up. the menu's chapter cfgs run client.dll's hc_new_game before their map
// (tools/halfcraft/game_folder.py), which tells them from a `map` typed into the console: that one starts no playthrough.

#include "core/hc_units.h"

namespace halfcraft
{
	class Checkpoints
	{
	public:
		/// IGameSystem::OnSave, before the entities are written: a new id into the save, and a
		/// checkpoint under it.
		void on_save();

		/// LevelInitPostEntity: a loaded save rolls minecraft back to its checkpoint; every level gets
		/// its checkpoint entity. a new game or a transition tells minecraft the map was entered.
		/// @param slot - where the map sits in minecraft
		void on_level_loaded(MapSlot slot);
	};
}

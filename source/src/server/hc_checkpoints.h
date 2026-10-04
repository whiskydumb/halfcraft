#pragma once

// server.dll: half-life's saves roll minecraft back too. every save carries a checkpoint id (in a
// logical entity saved with the level); saving tells minecraft to keep its world and player as they
// are under that id, and loading a save tells it to go back to them. level transitions restore the
// levels' entities as well but aren't loads, so they leave minecraft alone.

namespace halfcraft
{
	class Checkpoints
	{
	public:
		/// IGameSystem::OnSave, before the entities are written: a new id into the save, and a
		/// checkpoint under it.
		void on_save();

		/// LevelInitPostEntity: a loaded save rolls minecraft back to its checkpoint; every level gets
		/// its checkpoint entity.
		void on_level_loaded();
	};
}

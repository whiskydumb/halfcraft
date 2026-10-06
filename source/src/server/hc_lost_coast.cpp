// server.dll: lost coast runs in the same game as half-life 2 and the episodes, whose content is mounted
// over half-life 2's (source\mod\<engine>\gameinfo.txt). it was made with half-life 2's alone, which the
// game folder's content covers in two places:
// - the engine keeps one scenes/scenes.image, the first on GAME: episode two's, without lost coast's
//   scenes (its fisherman stood mute). on lost coast a copy of its own image goes into the game folder,
//   which is searched first, and the engine reloads it; any other map removes the copy (a crash on lost
//   coast leaves it behind) and reloads the campaigns'. a vpk already mounted can't be moved ahead on
//   GAME at run time: the file system skips it;
// - episode two's blackout.mdl, the first-person rig of its wake-up intros, has none of half-life 2's
//   sequences. lost coast's intro (e3_start) plays "exit1" on it and lets the player go only when that
//   animation finishes (OnAnimationDone enables trigger_knockout_teleport): with episode two's model the
//   player stayed frozen in the intro camera. hc_blackout.cpp puts episode one's model, which has it,
//   first; should episode two's still be the one loaded, the trigger comes on here when the animation
//   would have ended.

#include "cbase.h"
#include "eventqueue.h"
#include "filesystem.h"
#include "scenefilecache/ISceneFileCache.h"

#include "tier1/utlbuffer.h"

#include "core/hc_log.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ISceneFileCache* scenefilecache;

namespace
{
	constexpr const char* LOST_COAST_MAP = "d2_lostcoast";
	constexpr const char* SCENE_IMAGE = "scenes/scenes.image";
	constexpr const char* LOST_COAST_PAK = "hc_lc";             // its pak's own path id (gameinfo.txt)
	constexpr const char* GAME_FOLDER = "DEFAULT_WRITE_PATH";  // the game folder: first on GAME
	// e3_start plays exit1 4 s after the map spawns; half-life 2's runs 66 frames at 30 fps
	constexpr float INTRO_SECONDS = 4.0f + 66.0f / 30.0f;

	class LostCoast final : public CAutoGameSystem
	{
	public:
		LostCoast() : CAutoGameSystem("HalfCraftLostCoast") {}

		void LevelInitPreEntity() override
		{
			// the scenes go before the map's entities precache theirs
			const bool lost_coast = V_stricmp(STRING(gpGlobals->mapname), LOST_COAST_MAP) == 0;
			const bool copied = filesystem->FileExists(SCENE_IMAGE, GAME_FOLDER);
			if (lost_coast != copied) {
				load_scenes(lost_coast);
			}
		}

		void LevelInitPostEntity() override
		{
			if (V_stricmp(STRING(gpGlobals->mapname), LOST_COAST_MAP) != 0) {
				return;
			}
			CBaseEntity*    entity = gEntList.FindEntityByName(nullptr, "blackout");
			CBaseAnimating* blackout = entity ? entity->GetBaseAnimating() : nullptr;
			// a save from after the intro has no rig left
			if (!blackout || blackout->LookupSequence("exit1") != -1) {
				return;
			}
			variant_t none;
			g_EventQueue.AddEvent("trigger_knockout_teleport", "Enable", none, INTRO_SECONDS, nullptr, nullptr);
			halfcraft::log_info("lost coast: blackout.mdl has no exit1 (episode two's), so its intro lets the player go after %.1f s", INTRO_SECONDS);
		}

	private:
		/// puts lost coast's scenes.image in the game folder, or takes it out again, and has the engine
		/// read the one that's first on GAME.
		static void load_scenes(bool lost_coast)
		{
			if (lost_coast) {
				CUtlBuffer image;
				if (!filesystem->ReadFile(SCENE_IMAGE, LOST_COAST_PAK, image)) {
					halfcraft::log_warning("lost coast: no %s in its pak (path id %s); its scenes stay out", SCENE_IMAGE, LOST_COAST_PAK);
					return;
				}
				filesystem->CreateDirHierarchy("scenes", GAME_FOLDER);
				if (!filesystem->WriteFile(SCENE_IMAGE, GAME_FOLDER, image)) {
					halfcraft::log_warning("lost coast: couldn't write %s into the game folder; its scenes stay out", SCENE_IMAGE);
					return;
				}
			} else {
				filesystem->RemoveFile(SCENE_IMAGE, GAME_FOLDER);
			}
			scenefilecache->Reload();
			halfcraft::log_info("lost coast: %s scenes.image loaded", lost_coast ? "its own" : "the campaigns'");
		}
	};

	LostCoast g_lost_coast;
}

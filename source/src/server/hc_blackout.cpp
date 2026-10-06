// server.dll: models/blackout.mdl, the first-person rig of the knockouts and wake-ups, for half-life 2,
// the episodes and lost coast, whose content is mounted over each other (source\mod\<engine>\gameinfo.txt).
// episode two's comes first, and it's a rig of its own: none of the sequences the other campaigns play
// on it. half-life 2's d1_trainstation_04 and lost coast's intro play "exit1" and let the player go
// only when it ends (OnAnimationDone enables trigger_knockout_teleport): the player stayed frozen in
// the knockout camera. d3_breen_01's finale turns its view controller off the same way, and episode
// one's intro plays its own e1_dogintro. episode one's model has half-life 2's three sequences and
// its own, so off episode two's maps a copy of it goes into the game folder, which is searched
// first, and the model cache lets go of the one it read; on episode two's maps the copy goes again.

#include "cbase.h"
#include "datacache/imdlcache.h"
#include "filesystem.h"

#include "tier1/utlbuffer.h"

#include "core/hc_log.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
	constexpr const char* MODEL = "models/blackout.mdl";
	constexpr const char* MODEL_FILES[] = {
		"models/blackout.mdl", "models/blackout.vvd", "models/blackout.dx90.vtx", "models/blackout.dx80.vtx", "models/blackout.sw.vtx", "models/blackout.phy",
	};
	constexpr const char* EPISODE_ONE_PAK = "hc_ep1";           // its pak's own path id (gameinfo.txt)
	constexpr const char* GAME_FOLDER = "DEFAULT_WRITE_PATH";  // the game folder: first on GAME
	constexpr const char* EPISODE_TWO_PREFIX = "ep2_";

	class Blackout final : public CAutoGameSystem
	{
	public:
		Blackout() : CAutoGameSystem("HalfCraftBlackout") {}

		void LevelInitPreEntity() override
		{
			// before the map's entities precache the model
			const bool episode_two = V_strnicmp(STRING(gpGlobals->mapname), EPISODE_TWO_PREFIX, V_strlen(EPISODE_TWO_PREFIX)) == 0;
			const bool copied = filesystem->FileExists(MODEL, GAME_FOLDER);
			if (episode_two == copied) {
				use_episode_one(!episode_two);
			}
		}

	private:
		/// puts episode one's blackout model in the game folder, or takes it out again, and has the model
		/// cache read the one that's first on GAME next time.
		static void use_episode_one(bool episode_one)
		{
			for (const char* file : MODEL_FILES) {
				if (!episode_one) {
					filesystem->RemoveFile(file, GAME_FOLDER);
					continue;
				}
				CUtlBuffer data;
				if (!filesystem->ReadFile(file, EPISODE_ONE_PAK, data)) {
					halfcraft::log_warning("blackout: no %s in episode one's pak (path id %s); episode two's model stays", file, EPISODE_ONE_PAK);
					for (const char* written : MODEL_FILES) {
						filesystem->RemoveFile(written, GAME_FOLDER);
					}
					return;
				}
				filesystem->CreateDirHierarchy("models", GAME_FOLDER);
				if (!filesystem->WriteFile(file, GAME_FOLDER, data)) {
					halfcraft::log_warning("blackout: couldn't write %s into the game folder; episode two's model stays", file);
					return;
				}
			}
			const MDLHandle_t handle = mdlcache->FindMDL(MODEL);
			if (handle != MDLHANDLE_INVALID) {
				mdlcache->Flush(handle);
				mdlcache->Release(handle);
			}
			halfcraft::log_info("blackout: %s's knockout rig (models/blackout.mdl) is in use", episode_one ? "episode one" : "episode two");
		}
	};

	Blackout g_blackout;
}

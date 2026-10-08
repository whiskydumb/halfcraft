// server.dll: half-life 2, episode one and episode two run on the same episodic dlls, as valve's
// hl2_complete does, but each campaign keeps its own skill values. skill_manifest.cfg loads half-life 2's
// skill.cfg on every map; an episode's map adds that episode's skill_episodic.cfg on top before its npcs
// spawn (without it hunters, antlion workers and the advisor would read zeros). the game folder has the
// two as skill_ep1.cfg and skill_ep2.cfg (tools/engines.ps1). a map of another campaign than the episode
// last loaded first puts back what the episodes' files change, then runs skill_manifest.cfg again.

#include "cbase.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"

#include "tier0/valve_minmax_off.h"
#include <string>
#include <vector>

#include "core/hc_campaign.h"
#include "core/hc_log.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
	using halfcraft::Campaign;

	/// the skill cfg an episode's maps add, none for half-life 2's.
	const char* episode_skill(Campaign campaign)
	{
		switch (campaign) {
		case Campaign::kEpisodeOne:
			return "skill_ep1.cfg";
		case Campaign::kEpisodeTwo:
			return "skill_ep2.cfg";
		case Campaign::kHalfLife2:
			break;
		}
		return nullptr;
	}

	/// adds the console variables a cfg sets: the first word of each line that isn't a comment.
	void add_cvars(const char* cfg, std::vector<std::string>& names)
	{
		CUtlBuffer file(0, 0, CUtlBuffer::TEXT_BUFFER);
		if (!filesystem->ReadFile((std::string("cfg/") + cfg).c_str(), "GAME", file)) {
			halfcraft::log_warning("campaign skill: no cfg/%s", cfg);
			return;
		}
		char line[512];
		while (file.IsValid() && file.GetBytesRemaining() > 0) {
			file.GetLine(line, sizeof(line));
			const char* word = line;
			while (*word == ' ' || *word == '\t') {
				++word;
			}
			if (*word == '"') {
				++word;
			}
			std::size_t length = 0;
			while (word[length] && word[length] != ' ' && word[length] != '\t' && word[length] != '"' && word[length] != '\r' && word[length] != '\n') {
				++length;
			}
			if (length && word[0] != '#' && (word[0] != '/' || word[1] != '/')) {
				names.emplace_back(word, length);
			}
		}
	}

	void execute(const std::string& command)
	{
		engine->ServerCommand((command + "\n").c_str());
		engine->ServerExecute();
	}

	class CampaignSkill final : public CAutoGameSystem
	{
	public:
		CampaignSkill() : CAutoGameSystem("HalfCraftCampaignSkill") {}

		// the game rules have run skill_manifest.cfg by now (CWorld::Precache), and the map's entities
		// spawn after this: saves, transitions and new maps all come through here
		void LevelInitPreEntity() override
		{
			const char*    map = STRING(gpGlobals->mapname);
			const Campaign campaign = halfcraft::campaign_of(map);
			if (episode_skill(loaded_) && campaign != loaded_) {
				// skill_manifest.cfg sets most of them back, not the ones only the episodes use
				std::vector<std::string> names;
				add_cvars(episode_skill(Campaign::kEpisodeOne), names);
				add_cvars(episode_skill(Campaign::kEpisodeTwo), names);
				for (const std::string& name : names) {
					ConVarRef variable(name.c_str());
					if (variable.IsValid()) {
						variable.SetValue(variable.GetDefault());
					}
				}
				execute("exec skill_manifest.cfg");
				halfcraft::log_info("campaign skill: half-life 2's again on %s", map);
			}
			loaded_ = campaign;
			// every time: a new game's skill_manifest.cfg has just set half-life 2's values over the episode's
			if (const char* skill = episode_skill(campaign)) {
				execute(std::string("exec ") + skill);
				halfcraft::log_info("campaign skill: %s on %s", skill, map);
			}
		}

	private:
		Campaign loaded_ = Campaign::kHalfLife2;  // the campaign whose skill values are set
	};

	CampaignSkill g_campaign_skill;
}

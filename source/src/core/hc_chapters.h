#pragma once

// which chapter a map belongs to. the game folder's cfg/chapter<N>.cfg (tools/halfcraft/game_folder.py numbers
// half-life 2's 1-14 with 9a, episode one's 15-19 and episode two's 20-26) each load their chapter's
// first map; a map further into a chapter belongs to the last chapter that starts at or before it in
// play order (campaign_order). engine-agnostic.

#include <cctype>
#include <string>
#include <vector>

#include "core/hc_units.h"

namespace halfcraft
{
	struct ChapterStart
	{
		std::string chapter;  // "1", "9a", ...
		std::string map;      // the map its cfg loads, lowercase
	};

	/// the map a chapter cfg loads: the word after "map" ("playvideo_exitcommand ep1_recap map ep2_outland_01").
	/// @return it in lowercase, or empty when the cfg loads none
	inline std::string chapter_cfg_map(const std::string& cfg)
	{
		std::vector<std::string> words;
		std::string              word;
		for (const char ch : cfg + "\n") {
			if (std::isspace(static_cast<unsigned char>(ch)) || ch == ';') {
				if (!word.empty()) {
					words.push_back(word);
					word.clear();
				}
			} else {
				word += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
		}
		for (std::size_t i = 0; i + 1 < words.size(); ++i) {
			if (words[i] == "map") {
				return words[i + 1];
			}
		}
		return {};
	}

	/// @param map - the map's bare name, lowercase ("d1_canals_08")
	/// @return its chapter ("4"), or empty when it's in none
	inline std::string chapter_of(const std::vector<ChapterStart>& chapters, const std::string& map)
	{
		for (const auto& start : chapters) {
			if (start.map == map) {
				return start.chapter;
			}
		}
		const int order = campaign_order(map.c_str());
		if (order < 0) {
			return {};
		}
		const ChapterStart* best = nullptr;
		int                 best_order = -1;
		for (const auto& start : chapters) {
			const int start_order = campaign_order(start.map.c_str());
			if (start_order >= 0 && start_order <= order && start_order > best_order) {
				best = &start;
				best_order = start_order;
			}
		}
		return best ? best->chapter : std::string();
	}
}

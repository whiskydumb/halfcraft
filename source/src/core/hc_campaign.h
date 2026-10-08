#pragma once

// half-life 2, episode one and episode two run as one game (valve's hl2_complete layout). where their
// content differs (skill values, titles.txt, credits.txt) halfcraft goes by the campaign of the map
// being played, which its name tells: episode one's maps start "ep1_", episode two's "ep2_".
// engine-agnostic.

#include <cctype>
#include <cstring>

namespace halfcraft
{
	enum class Campaign
	{
		kHalfLife2,
		kEpisodeOne,
		kEpisodeTwo,
	};

	/// the campaign a map belongs to.
	/// @param map - its name, bare ("ep1_c17_00") or as a path ("maps/ep1_c17_00.bsp")
	inline Campaign campaign_of(const char* map)
	{
		if (!map) {
			return Campaign::kHalfLife2;
		}
		const char* slash = std::strrchr(map, '/');
		const char* backslash = std::strrchr(map, '\\');
		const char* name = slash && slash > backslash ? slash + 1 : backslash ? backslash + 1
																			  : map;
		const auto  starts = [name](const char* prefix) {
			for (std::size_t i = 0; prefix[i]; ++i) {
				if (!name[i] || std::tolower(static_cast<unsigned char>(name[i])) != prefix[i]) {
					return false;
				}
			}
			return true;
		};
		return starts("ep1_") ? Campaign::kEpisodeOne : starts("ep2_") ? Campaign::kEpisodeTwo
																	   : Campaign::kHalfLife2;
	}

	/// the search path id gameinfo.txt mounts that campaign's own content under (hc_hl2, hc_ep1, hc_ep2),
	/// for the files the campaigns have under the same name.
	inline const char* campaign_path_id(Campaign campaign)
	{
		switch (campaign) {
		case Campaign::kEpisodeOne:
			return "hc_ep1";
		case Campaign::kEpisodeTwo:
			return "hc_ep2";
		case Campaign::kHalfLife2:
			break;
		}
		return "hc_hl2";
	}
}

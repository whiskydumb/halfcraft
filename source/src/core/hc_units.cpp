#include "hc_units.h"

#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace halfcraft
{
	namespace
	{
		// half-life 2, episode one and episode two in play order. a map keeps its slot (and so its
		// builds) even if this list grows: only ever append.
		constexpr const char* CAMPAIGN_MAPS[] = {
			"d1_trainstation_01", "d1_trainstation_02", "d1_trainstation_03", "d1_trainstation_04", "d1_trainstation_05",
			"d1_trainstation_06", "d1_canals_01", "d1_canals_01a", "d1_canals_02", "d1_canals_03", "d1_canals_05",
			"d1_canals_06", "d1_canals_07", "d1_canals_08", "d1_canals_09", "d1_canals_10", "d1_canals_11", "d1_canals_12",
			"d1_canals_13", "d1_eli_01", "d1_eli_02", "d1_town_01", "d1_town_01a", "d1_town_02", "d1_town_03", "d1_town_02a",
			"d1_town_04", "d1_town_05", "d2_coast_01", "d2_coast_03", "d2_coast_04", "d2_coast_05", "d2_coast_07",
			"d2_coast_08", "d2_coast_09", "d2_coast_10", "d2_coast_11", "d2_coast_12", "d2_prison_01", "d2_prison_02",
			"d2_prison_03", "d2_prison_04", "d2_prison_05", "d2_prison_06", "d2_prison_07", "d2_prison_08", "d3_c17_01",
			"d3_c17_02", "d3_c17_03", "d3_c17_04", "d3_c17_05", "d3_c17_06a", "d3_c17_06b", "d3_c17_07", "d3_c17_08",
			"d3_c17_09", "d3_c17_10a", "d3_c17_10b", "d3_c17_11", "d3_c17_12", "d3_c17_12b", "d3_c17_13", "d3_citadel_01",
			"d3_citadel_02", "d3_citadel_03", "d3_citadel_04", "d3_citadel_05", "d3_breen_01", "ep1_citadel_00",
			"ep1_citadel_01", "ep1_citadel_02", "ep1_citadel_02b", "ep1_citadel_03", "ep1_citadel_04", "ep1_c17_00",
			"ep1_c17_00a", "ep1_c17_01", "ep1_c17_01a", "ep1_c17_02", "ep1_c17_02b", "ep1_c17_02a", "ep1_c17_05",
			"ep1_c17_06", "ep2_outland_01", "ep2_outland_01a", "ep2_outland_02", "ep2_outland_03", "ep2_outland_04",
			"ep2_outland_05", "ep2_outland_06", "ep2_outland_06a", "ep2_outland_07", "ep2_outland_08", "ep2_outland_09",
			"ep2_outland_10", "ep2_outland_10a", "ep2_outland_11", "ep2_outland_11a", "ep2_outland_11b", "ep2_outland_12",
			"ep2_outland_12a",
		};
		constexpr int FIRST_OTHER_SLOT = 256;
		constexpr int OTHER_SLOTS = 1024;

		/// "maps/d1_trainstation_01.bsp" or "D1_TrainStation_01" -> "d1_trainstation_01"
		std::string base_name(const char* map_name)
		{
			std::string name = map_name ? map_name : "";
			if (const auto slash = name.find_last_of("/\\"); slash != std::string::npos) {
				name.erase(0, slash + 1);
			}
			if (const auto dot = name.rfind(".bsp"); dot != std::string::npos && dot + 4 == name.size()) {
				name.erase(dot);
			}
			for (auto& ch : name) {
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			}
			return name;
		}

		std::uint32_t fnv1a(const std::string& text)
		{
			std::uint32_t hash = 2166136261u;
			for (const unsigned char ch : text) {
				hash = (hash ^ ch) * 16777619u;
			}
			return hash;
		}
	}

	float mc_fov_to_source(float vertical_degrees)
	{
		constexpr double DEG = 3.14159265358979323846 / 180.0;
		const double half = std::atan(std::tan(vertical_degrees * 0.5 * DEG) * (4.0 / 3.0));
		return static_cast<float>(half * 2.0 / DEG);
	}

	std::uint32_t map_world_id(const char* map_name)
	{
		const auto id = fnv1a(base_name(map_name));
		return id ? id : 1u;
	}

	int map_slot(const char* map_name)
	{
		const auto name = base_name(map_name);
		int        index = 0;
		for (const char* campaign : CAMPAIGN_MAPS) {
			if (name == campaign) {
				return index;
			}
			++index;
		}
		return FIRST_OTHER_SLOT + static_cast<int>(fnv1a(name) % OTHER_SLOTS);
	}
}

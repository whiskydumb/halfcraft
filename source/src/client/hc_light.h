#pragma once

// client.dll: how minecraft's things are lit in half-life. the map's light where they are (the
// lightmaps and ambient cubes models get, through the engine's light query), gamma corrected and
// scaled by hc_block_light, or minecraft's own block light (torches, lava) where that's brighter.
// include after the sdk headers and tier0/valve_minmax_off.h.

#include <cstdint>
#include <unordered_map>

namespace halfcraft
{
	// minecraft's fixed face brightness (CardinalLighting.DEFAULT), by Direction ordinal + 1
	// (0 = no face: unshaded)
	inline constexpr float FACE_SHADE[7] = { 1.0f, 0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f };
	// face normals in minecraft axes: none, down, up, north (-z), south (+z), west (-x), east (+x)
	inline constexpr float FACE_NORMAL[7][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { -1, 0, 0 }, { 1, 0, 0 } };

	/// hc_block_light: how much brighter than the raw light sample minecraft's things are drawn.
	float light_scale();

	/// the map's light at points, cached on a quarter-block grid (one cache per mesh build or frame).
	class LightCache
	{
	public:
		explicit LightCache(float scale) : scale_(scale) {}

		/// @param point - source units
		/// @return gamma-space light per channel, about 0..1
		Vector at(const float point[3]);

	private:
		float                                     scale_;
		std::unordered_map<std::uint64_t, Vector> cache_;
	};

	/// a vertex colour: rgba's rgb times shade times the brighter of the map's light and
	/// minecraft's block light; alpha kept.
	/// @param block_light - minecraft's block light, 0-15
	void lit_color(const std::uint8_t rgba[4], float shade, const Vector& map_light, int block_light, std::uint8_t out[4]);
}

// client.dll: how minecraft's things are lit in half-life (see hc_light.h).

#include "cbase.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>
#include <cmath>

#include "client/hc_light.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		constexpr float MIN_LIGHT = 0.03f;  // pitch black reads as missing geometry
		constexpr float TORCH_COLOR[3] = { 1.0f, 0.85f, 0.6f };
		constexpr float CACHE_STEP = 10.0f;  // units: a quarter block

		ConVar hc_block_light("hc_block_light", "2", FCVAR_ARCHIVE, "halfcraft: brightness of minecraft's blocks in the map's light (source's own overbright is 2)");

		float to_gamma(float linear)
		{
			return std::pow(std::clamp(linear, 0.0f, 1.0f), 1.0f / 2.2f);
		}
	}

	float light_scale()
	{
		return std::max(0.0f, hc_block_light.GetFloat());
	}

	Vector LightCache::at(const float point[3])
	{
		const auto          qx = static_cast<std::int64_t>(std::floor(point[0] / CACHE_STEP)), qy = static_cast<std::int64_t>(std::floor(point[1] / CACHE_STEP)),
							qz = static_cast<std::int64_t>(std::floor(point[2] / CACHE_STEP));
		const std::uint64_t key = (std::uint64_t(qx & 0x1FFFFF) << 42) | (std::uint64_t(qy & 0x1FFFFF) << 21) | std::uint64_t(qz & 0x1FFFFF);
		const auto          it = cache_.find(key);
		if (it != cache_.end()) {
			return it->second;
		}
		const Vector linear = engine->GetLightForPointFast(Vector(point[0], point[1], point[2]), true);
		const Vector light(to_gamma(linear.x * scale_), to_gamma(linear.y * scale_), to_gamma(linear.z * scale_));
		cache_.emplace(key, light);
		return light;
	}

	void lit_color(const std::uint8_t rgba[4], float shade, const Vector& map_light, int block_light, std::uint8_t out[4])
	{
		const float block = static_cast<float>(std::clamp(block_light, 0, 15)) / 15.0f;
		for (int c = 0; c < 3; ++c) {
			const float light = std::max({ map_light[c], block * TORCH_COLOR[c], MIN_LIGHT });
			out[c] = static_cast<std::uint8_t>(std::clamp(rgba[c] * shade * light, 0.0f, 255.0f));
		}
		out[3] = rgba[3];
	}
}

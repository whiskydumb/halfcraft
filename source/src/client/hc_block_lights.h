#pragma once

// client.dll: minecraft's light-emitting blocks (torches, lava, glowstone, ...) as source lights. a
// few (hc_torch_light_count: the ones whose light is on screen first, then the nearest) are point
// lights made of source's projected textures (its flashlight), so they light half-life's world per
// pixel in their colour, with shadows; they fade in and out as they change hands. characters get
// every nearby light as an elight. minecraft's own blocks carry its block light in their vertex
// colours already. hc_debug_torch puts one where the player looks, for tuning without minecraft.

#include <cstdint>

namespace halfcraft
{
	/// one render-ring message (main thread): kRenLights and kRenClearAll; ignores the rest.
	void block_lights_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes);

	struct BlockLightStats
	{
		std::uint32_t emitters = 0;  // minecraft's light-emitting blocks we know of
		std::uint32_t lights = 0;    // lights made of them around the player
		std::uint32_t shadowed = 0;  // of those, the shadowed point lights
	};

	/// what the block lights are doing (main thread), for minecraft's debug screen.
	BlockLightStats block_lights_stats();
}

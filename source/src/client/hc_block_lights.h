#pragma once

// client.dll: minecraft's light-emitting blocks (torches, lava, glowstone, ...) as source dynamic
// lights, so they light half-life's world and its characters the way a flare would. the nearest
// few light the world (dlights: they cost lightmap updates), the next ones only characters
// (elights). minecraft's own blocks carry its block light in their vertex colours already.
// ported from SkyCraft's BlockLights.cpp.

#include <cstdint>

namespace halfcraft
{
	/// one render-ring message (main thread): kRenLights and kRenClearAll; ignores the rest.
	void block_lights_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes);
}

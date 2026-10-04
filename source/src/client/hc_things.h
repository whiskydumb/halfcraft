#pragma once

// client.dll: what minecraft shows besides its blocks, drawn in source's world every frame.
// - the world entities minecraft publishes: dropped items (icons, or small spinning cubes for
//   blocks), thrown things, arrows and tridents, block cracks, the outline of the targeted block;
// - the render ring's scene: whatever minecraft's entity renderer and particle engine draw (lit
//   tnt, falling blocks, minecarts, chests, signs, particles, ...), with its entity textures.
// all of it is one source renderable lit by the map's light like the blocks (hc_light.h).

#include <cstdint>

namespace halfcraft
{
	/// one render-ring message (main thread): entity textures, the scene and kRenClearAll; ignores
	/// the rest.
	void things_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes);
}

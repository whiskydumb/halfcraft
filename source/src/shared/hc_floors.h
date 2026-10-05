#pragma once

// client.dll and server.dll: the height of a map's block grid (core/hc_grid.h), read from its .bsp.
// both dlls read the same file with the same code, so they agree without telling each other.

namespace halfcraft
{
	/// the map's most common floor as the height of minecraft's block grid. logs it ("grid offset
	/// for <map>: ..."). 0 (the grid as it was before maps had one) when the map can't be read.
	/// @param map_name - "d1_canals_01" or "maps/d1_canals_01.bsp"
	/// @return source z of minecraft's y = 0 (whole units)
	float map_grid_z(const char* map_name);
}

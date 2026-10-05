#pragma once

// client.dll: what minecraft's debug screen (F3) shows about half-life (proto::HostDebug), written a
// few times a second: the map and its chapter, where the player is and looks, half-life's frame rate,
// minecraft's block lights here and how the link is doing. server.dll adds what's under the
// crosshair (proto::HostDebugServer, server/hc_debug_target.h).

namespace halfcraft
{
	/// what drawing minecraft's overlay cost a frame lately (hc_overlay.cpp).
	/// @param frames - averaged over this many of the last frames it was drawn
	/// @return milliseconds; 0 before it was first drawn
	float overlay_cost_ms(int frames);
}

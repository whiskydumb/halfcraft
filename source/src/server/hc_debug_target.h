#pragma once

// server.dll: its part of minecraft's debug screen (proto::HostDebugServer): what's under the
// crosshair (its class, name and health, and for an npc how it feels about the player and what it's
// doing) and how many edicts are in use. npc health isn't networked, so client.dll can't tell.

#include "core/hc_link.h"

class CBasePlayer;

namespace halfcraft
{
	class DebugTarget
	{
	public:
		/// once per frame; writes a few times a second while minecraft is there.
		void update(Link& link, CBasePlayer* player);

	private:
		double next_publish_ = 0.0;
		int    logged_npc_ = 0;  // the npc last logged as targeted (entity handle, CBaseHandle::ToInt)
	};
}

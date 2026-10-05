#pragma once

// server.dll: half-life's weapons as minecraft items (#12). source's inventory is the authority.
//
//   table      the local player's weapons, their ammo and the one it has out go to minecraft
//              (proto::WeaponTable), which keeps one item per weapon in its inventory
//   choice     minecraft's hand picks the weapon. the client's commands say so (HC_CMD_WEAPONS):
//              source's own weapon selection takes out the one in weaponselect, this puts the active
//              one away when the hand holds none it can take out, and source doesn't take one out by
//              itself (server_minecraft_picks_weapon: one picked up, or ammo for a dry one)
//   viewmodel  while minecraft's hud is up, half-life's viewmodel only shows the weapon minecraft
//              holds, seen through the player's eyes (HC_CMD_VIEWMODEL)

#include "core/hc_link.h"

class CBasePlayer;

namespace halfcraft
{
	class Weapons
	{
	public:
		/// once per frame.
		/// @param player - the local player (nullptr: none), whose weapons minecraft shows
		/// @param minecraft_hud - minecraft's hud is up instead of half-life's
		void update(Link& link, CBasePlayer* player, bool minecraft_hud);

	private:
		void write_table(Link& link, CBasePlayer* player);

		proto::WeaponTable sent_{};
		bool               ever_sent_ = false;
	};
}

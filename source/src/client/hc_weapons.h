#pragma once

// client.dll: half-life's weapons as minecraft items (#12; server.dll's half is server/hc_weapons.cpp).
// minecraft's hand picks the weapon: every command asks source for the one minecraft holds
// (weaponselect, HC_CMD_WEAPONS), or for none. its mouse buttons and R fire, alt-fire and reload
// it, but only once source really has that weapon out (a switch can be refused or take a moment).
// include after the sdk headers.

class CUserCmd;

namespace halfcraft
{
	struct ClientSession;

	/// minecraft's main hand holds one of half-life's weapons: the mouse buttons and R are its.
	bool weapon_in_hand(const ClientSession& session);

	/// ClientModeShared::CreateMove (client_create_move, before anything else): the weapon source
	/// should have out, and whether its viewmodel shows.
	void weapon_create_move(ClientSession& session, CUserCmd* cmd);

	/// the attack and reload buttons source gets in this command (after weapon_create_move): a prop
	/// carried with use is thrown or dropped, else the weapon minecraft holds fires once it's out.
	int weapon_buttons(const ClientSession& session);

	/// minecraft's field of view with half-life's zoom on top (the crossbow's scope).
	/// @param fov - source degrees
	float weapon_zoom_fov(float fov);
}

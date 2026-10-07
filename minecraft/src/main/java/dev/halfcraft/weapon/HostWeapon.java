package dev.halfcraft.weapon;

import dev.halfcraft.link.Proto;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's weapons the player can own (WeaponId in the protocol header), each with the Minecraft
 * item that stands in for it in the hotbar.
 */
public enum HostWeapon {
	CROWBAR(Proto.HOST_WEAPON_CROWBAR, "crowbar"),
	GRAVITY_GUN(Proto.HOST_WEAPON_PHYSCANNON, "gravity_gun"),
	PISTOL(Proto.HOST_WEAPON_PISTOL, "pistol"),
	REVOLVER(Proto.HOST_WEAPON_357, "revolver"),
	SMG(Proto.HOST_WEAPON_SMG1, "smg"),
	PULSE_RIFLE(Proto.HOST_WEAPON_AR2, "pulse_rifle"),
	SHOTGUN(Proto.HOST_WEAPON_SHOTGUN, "shotgun"),
	CROSSBOW(Proto.HOST_WEAPON_CROSSBOW, "crossbow"),
	GRENADE(Proto.HOST_WEAPON_FRAG, "grenade"),
	RPG(Proto.HOST_WEAPON_RPG, "rpg"),
	BUGBAIT(Proto.HOST_WEAPON_BUGBAIT, "bugbait");

	private final int id;
	private final String path;

	HostWeapon(int id, String path) {
		this.id = id;
		this.path = path;
	}

	/** The protocol's WeaponId. */
	public int id() {
		return this.id;
	}

	/**
	 * The stand-in item's path in the halfcraft namespace: its model, its texture and its name in the
	 * language files (item.halfcraft.crowbar) are named after it.
	 */
	public String path() {
		return this.path;
	}

	public static @Nullable HostWeapon byId(int id) {
		for (HostWeapon weapon : values()) {
			if (weapon.id == id) {
				return weapon;
			}
		}
		return null;
	}
}

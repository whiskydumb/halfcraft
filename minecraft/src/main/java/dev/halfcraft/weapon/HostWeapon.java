package dev.halfcraft.weapon;

import dev.halfcraft.link.Proto;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's weapons the player can own (WeaponId in the protocol header), each with the Minecraft
 * item that stands in for it in the hotbar.
 */
public enum HostWeapon {
	CROWBAR(Proto.HOST_WEAPON_CROWBAR, "crowbar", "Crowbar"),
	GRAVITY_GUN(Proto.HOST_WEAPON_PHYSCANNON, "gravity_gun", "Gravity Gun"),
	PISTOL(Proto.HOST_WEAPON_PISTOL, "pistol", "9mm Pistol"),
	REVOLVER(Proto.HOST_WEAPON_357, "revolver", ".357 Magnum"),
	SMG(Proto.HOST_WEAPON_SMG1, "smg", "SMG"),
	PULSE_RIFLE(Proto.HOST_WEAPON_AR2, "pulse_rifle", "Pulse Rifle"),
	SHOTGUN(Proto.HOST_WEAPON_SHOTGUN, "shotgun", "Shotgun"),
	CROSSBOW(Proto.HOST_WEAPON_CROSSBOW, "crossbow", "Crossbow"),
	GRENADE(Proto.HOST_WEAPON_FRAG, "grenade", "Grenade"),
	RPG(Proto.HOST_WEAPON_RPG, "rpg", "RPG"),
	BUGBAIT(Proto.HOST_WEAPON_BUGBAIT, "bugbait", "Bugbait");

	private final int id;
	private final String path;
	private final String displayName;

	HostWeapon(int id, String path, String displayName) {
		this.id = id;
		this.path = path;
		this.displayName = displayName;
	}

	/** The protocol's WeaponId. */
	public int id() {
		return this.id;
	}

	/** The stand-in item's path in the halfcraft namespace (its model and texture are named after it). */
	public String path() {
		return this.path;
	}

	/** What Half-Life calls it. */
	public String displayName() {
		return this.displayName;
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

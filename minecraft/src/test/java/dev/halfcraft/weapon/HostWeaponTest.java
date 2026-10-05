package dev.halfcraft.weapon;

import static dev.halfcraft.link.Proto.*;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Set;
import org.junit.jupiter.api.Test;

class HostWeaponTest {
	private static final Path ASSETS = Path.of("src/main/resources/assets/halfcraft");

	@Test
	void everyWeaponIdHasOneItem() {
		Set<Integer> ids = new HashSet<>();
		for (HostWeapon weapon : HostWeapon.values()) {
			assertTrue(ids.add(weapon.id()), weapon + " shares its id");
			assertEquals(weapon, HostWeapon.byId(weapon.id()));
		}
		for (int id = HOST_WEAPON_CROWBAR; id <= HOST_WEAPON_BUGBAIT; id++) {
			assertTrue(ids.contains(id), "no item for weapon id " + id);
		}
		assertNull(HostWeapon.byId(HOST_WEAPON_NONE));
	}

	@Test
	void everyItemHasItsModelAndTexture() {
		for (HostWeapon weapon : HostWeapon.values()) {
			assertTrue(Files.isRegularFile(ASSETS.resolve("items/" + weapon.path() + ".json")), weapon.path());
			assertTrue(Files.isRegularFile(ASSETS.resolve("models/item/" + weapon.path() + ".json")), weapon.path());
			assertTrue(Files.isRegularFile(ASSETS.resolve("textures/item/" + weapon.path() + ".png")), weapon.path());
		}
	}
}

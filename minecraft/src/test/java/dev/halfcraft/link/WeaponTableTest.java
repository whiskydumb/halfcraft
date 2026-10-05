package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.JAVA_INT;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import org.junit.jupiter.api.Test;

class WeaponTableTest {
	/** A weapon table laid out the way Half-Life's server.dll writes one. */
	private static MemorySegment table(int seq, boolean live, int active, int[][] records) {
		MemorySegment s = Arena.ofAuto().allocate(WEAPON_TABLE_BYTES);
		s.set(JAVA_INT, WT_SEQ, seq);
		s.set(JAVA_INT, WT_FLAGS, live ? WT_LIVE : 0);
		s.set(JAVA_INT, WT_COUNT, records.length);
		s.set(JAVA_INT, WT_ACTIVE, active);
		for (int i = 0; i < records.length; i++) {
			for (int field = 0; field < 8; field++) {
				s.set(JAVA_INT, WT_RECORDS + i * WT_RECORD_BYTES + field * 4L, records[i][field]);
			}
		}
		return s;
	}

	@Test
	void readsWhatHalfLifeWrote() {
		int[] smg = { HOST_WEAPON_SMG1, 30, 45, 150, 225, 2, 3, 0 };
		int[] gravityGun = { HOST_WEAPON_PHYSCANNON, -1, -1, -1, -1, -1, -1, WT_R_SUPERCHARGED };
		WeaponTable table = WeaponTable.read(table(4, true, HOST_WEAPON_SMG1, new int[][] { smg, gravityGun }), 0);
		assertTrue(table.live());
		assertEquals(HOST_WEAPON_SMG1, table.active());
		assertEquals(new WeaponTable.Weapon(HOST_WEAPON_SMG1, 30, 45, 150, 225, 2, 3, 0), table.find(HOST_WEAPON_SMG1));
		assertTrue(table.find(HOST_WEAPON_PHYSCANNON).supercharged());
		assertNull(table.find(HOST_WEAPON_RPG));
	}

	@Test
	void aTableNeverWrittenSaysNothing() {
		assertSame(WeaponTable.NONE, WeaponTable.read(table(0, true, 0, new int[0][]), 0));
	}

	@Test
	void aTableBeingWrittenIsNotRead() {
		assertNull(WeaponTable.read(table(5, true, 0, new int[0][]), 0));
	}

	@Test
	void aTableWithoutAPlayerIsNotLive() {
		assertFalse(WeaponTable.read(table(2, false, 0, new int[0][]), 0).live());
	}

	@Test
	void clipWeaponsShowTheirClip() {
		WeaponTable.Weapon pistol = new WeaponTable.Weapon(HOST_WEAPON_PISTOL, 9, 18, 150, 150, -1, -1, 0);
		assertEquals(0.5F, pistol.fill());
		assertEquals("9 | 150", pistol.ammoLine());
	}

	@Test
	void weaponsWithoutAClipShowTheirReserve() {
		WeaponTable.Weapon rpg = new WeaponTable.Weapon(HOST_WEAPON_RPG, -1, -1, 2, 3, -1, -1, 0);
		assertEquals(2, rpg.ready());
		assertEquals(2.0F / 3.0F, rpg.fill());
		assertEquals("2", rpg.ammoLine());
	}

	@Test
	void anEmptyClipIsEmptyNotMissing() {
		WeaponTable.Weapon shotgun = new WeaponTable.Weapon(HOST_WEAPON_SHOTGUN, 0, 6, 0, 30, -1, -1, 0);
		assertEquals(0.0F, shotgun.fill());
		assertEquals("0 | 0", shotgun.ammoLine());
	}

	@Test
	void weaponsWithoutAmmoShowNone() {
		WeaponTable.Weapon crowbar = new WeaponTable.Weapon(HOST_WEAPON_CROWBAR, -1, -1, -1, -1, -1, -1, 0);
		assertEquals(-1.0F, crowbar.fill());
		assertEquals("", crowbar.ammoLine());
	}

	@Test
	void secondaryAmmoComesLast() {
		WeaponTable.Weapon ar2 = new WeaponTable.Weapon(HOST_WEAPON_AR2, 30, 30, 60, 60, 1, 3, 0);
		assertEquals("30 | 60 | 1", ar2.ammoLine());
	}
}

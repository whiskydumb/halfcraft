package dev.halfcraft.weapon;

import static dev.halfcraft.link.Proto.*;
import static dev.halfcraft.weapon.WeaponSync.EMPTY;
import static dev.halfcraft.weapon.WeaponSync.OFFHAND;
import static dev.halfcraft.weapon.WeaponSync.OTHER;
import static org.junit.jupiter.api.Assertions.assertEquals;

import java.util.Arrays;
import java.util.List;
import java.util.Map;
import org.junit.jupiter.api.Test;

class WeaponSyncTest {
	/** An inventory (main, armour, offhand) holding nothing. */
	private static int[] emptyInventory() {
		int[] slots = new int[OFFHAND + 1];
		Arrays.fill(slots, EMPTY);
		return slots;
	}

	@Test
	void addsANewWeaponToTheFirstFreeHotbarSlot() {
		int[] slots = emptyInventory();
		slots[0] = OTHER;
		slots[1] = OTHER;
		assertEquals(List.of(new WeaponSync.Add(HOST_WEAPON_PHYSCANNON, 2)), WeaponSync.plan(slots, new int[] { HOST_WEAPON_PHYSCANNON }, 0, Map.of()));
	}

	@Test
	void removesAWeaponHalfLifeNoLongerHas() {
		int[] slots = emptyInventory();
		slots[3] = HOST_WEAPON_RPG;
		slots[4] = HOST_WEAPON_PHYSCANNON;
		assertEquals(List.of(new WeaponSync.Remove(3)), WeaponSync.plan(slots, new int[] { HOST_WEAPON_PHYSCANNON }, 0, Map.of()));
	}

	@Test
	void putsADroppedStandInBackWhereItWas() {
		int[] slots = emptyInventory();
		assertEquals(List.of(new WeaponSync.Add(HOST_WEAPON_RPG, 4)),
			WeaponSync.plan(slots, new int[] { HOST_WEAPON_RPG }, 0, Map.of(HOST_WEAPON_RPG, 4)));
	}

	@Test
	void undoesASwapIntoTheOffhand() {
		int[] slots = emptyInventory();
		slots[5] = OTHER;  // the shield, swapped out of the offhand
		slots[OFFHAND] = HOST_WEAPON_SMG1;
		assertEquals(List.of(new WeaponSync.Swap(OFFHAND, 5)), WeaponSync.plan(slots, new int[] { HOST_WEAPON_SMG1 }, 0, Map.of(HOST_WEAPON_SMG1, 5)));
	}

	@Test
	void movesAnOffhandStandInWithoutAKnownSlotToTheFirstFreeOne() {
		int[] slots = emptyInventory();
		slots[0] = OTHER;
		slots[OFFHAND] = HOST_WEAPON_SMG1;
		assertEquals(List.of(new WeaponSync.Swap(OFFHAND, 1)), WeaponSync.plan(slots, new int[] { HOST_WEAPON_SMG1 }, 0, Map.of()));
	}

	@Test
	void keepsTheCopyWhereTheWeaponWasLastSeen() {
		int[] slots = emptyInventory();
		slots[1] = HOST_WEAPON_PHYSCANNON;
		slots[7] = HOST_WEAPON_PHYSCANNON;
		assertEquals(List.of(new WeaponSync.Remove(1)), WeaponSync.plan(slots, new int[] { HOST_WEAPON_PHYSCANNON }, 0, Map.of(HOST_WEAPON_PHYSCANNON, 7)));
	}

	@Test
	void waitsForRoomWhenTheInventoryIsFull() {
		int[] slots = emptyInventory();
		Arrays.fill(slots, 0, WeaponSync.MAIN_SLOTS, OTHER);
		assertEquals(List.of(), WeaponSync.plan(slots, new int[] { HOST_WEAPON_RPG }, 0, Map.of()));
	}

	@Test
	void countsTheCursorAsInTheInventory() {
		int[] slots = emptyInventory();
		assertEquals(List.of(), WeaponSync.plan(slots, new int[] { HOST_WEAPON_PHYSCANNON }, HOST_WEAPON_PHYSCANNON, Map.of()));
	}

	@Test
	void clearsACursorCopyOfAWeaponAlreadyInASlot() {
		int[] slots = emptyInventory();
		slots[0] = HOST_WEAPON_PHYSCANNON;
		assertEquals(List.of(new WeaponSync.ClearCarried()), WeaponSync.plan(slots, new int[] { HOST_WEAPON_PHYSCANNON }, HOST_WEAPON_PHYSCANNON, Map.of()));
	}

	@Test
	void leavesAnInventoryThatMatchesAlone() {
		int[] slots = emptyInventory();
		slots[0] = HOST_WEAPON_CROWBAR;
		slots[1] = HOST_WEAPON_PHYSCANNON;
		slots[2] = OTHER;
		assertEquals(List.of(), WeaponSync.plan(slots, new int[] { HOST_WEAPON_PHYSCANNON, HOST_WEAPON_CROWBAR }, 0, Map.of()));
	}

	@Test
	void stripsEverythingWhenHalfLifeTakesTheWeaponsAway() {
		int[] slots = emptyInventory();
		slots[0] = HOST_WEAPON_CROWBAR;
		slots[3] = HOST_WEAPON_SMG1;
		slots[8] = HOST_WEAPON_RPG;
		assertEquals(List.of(new WeaponSync.Remove(0), new WeaponSync.Remove(3), new WeaponSync.Remove(8)), WeaponSync.plan(slots, new int[0], 0, Map.of()));
	}
}

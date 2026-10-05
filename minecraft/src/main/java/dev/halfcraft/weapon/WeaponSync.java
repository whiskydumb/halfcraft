package dev.halfcraft.weapon;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * What to change in a player's inventory so it holds exactly one stand-in item for each weapon
 * Half-Life lists, and none outside the main inventory. Pure: slots are weapon ids, not items.
 *
 * <p>A stand-in that went missing (dropped, deleted in the creative inventory, rolled back) comes
 * back to the slot it was last seen in when that is free, else to the first free slot, hotbar
 * first; one swapped into the offhand (F) swaps back.
 */
public final class WeaponSync {
	/** A slot holding nothing. */
	public static final int EMPTY = -1;
	/** A slot holding something that isn't a stand-in. */
	public static final int OTHER = 0;
	/** Slots 0-35 are the main inventory (0-8 the hotbar); the ones after it are worn or the offhand. */
	public static final int MAIN_SLOTS = 36;
	/** The offhand's slot. */
	public static final int OFFHAND = 40;

	private WeaponSync() {
	}

	public sealed interface Change permits Add, Remove, Swap, ClearCarried {
	}

	/** Put a new stand-in for {@code weapon} into {@code slot} (empty). */
	public record Add(int weapon, int slot) implements Change {
	}

	/** Take the stand-in out of {@code slot}. */
	public record Remove(int slot) implements Change {
	}

	/** Exchange what two slots hold (a stand-in going back where it belongs). */
	public record Swap(int from, int to) implements Change {
	}

	/** Empty the cursor (the stand-in on it is one too many, or Half-Life no longer has it). */
	public record ClearCarried() implements Change {
	}

	/**
	 * @param slots    what each inventory slot holds: a weapon id, {@link #OTHER} or {@link #EMPTY}
	 * @param owned    the weapons Half-Life lists, in its order
	 * @param carried  the weapon on the cursor of an open inventory screen, or 0
	 * @param lastSlot where each weapon's stand-in was last seen (weapon id to slot)
	 * @return the changes, to apply in order
	 */
	public static List<Change> plan(int[] slots, int[] owned, int carried, Map<Integer, Integer> lastSlot) {
		int[] work = slots.clone();
		List<Change> changes = new ArrayList<>();
		Set<Integer> listed = new HashSet<>();
		for (int weapon : owned) {
			listed.add(weapon);
		}

		// one stand-in per weapon: the one where it was last seen if it's still there, else the first
		Map<Integer, Integer> kept = new HashMap<>();
		for (int weapon : listed) {
			Integer last = lastSlot.get(weapon);
			if (last != null && last >= 0 && last < work.length && work[last] == weapon) {
				kept.put(weapon, last);
			}
		}
		for (int slot = 0; slot < work.length; slot++) {
			int weapon = work[slot];
			if (weapon <= OTHER) {
				continue;
			}
			if (!listed.contains(weapon) || kept.getOrDefault(weapon, slot) != slot) {
				changes.add(new Remove(slot));
				work[slot] = EMPTY;
			} else {
				kept.put(weapon, slot);
			}
		}
		if (carried > OTHER) {
			if (!listed.contains(carried) || kept.containsKey(carried)) {
				changes.add(new ClearCarried());
			} else {
				kept.put(carried, -1);
			}
		}

		// worn or in the offhand: back into the main inventory. out of the offhand it swaps with
		// whatever took its place (undoing F)
		for (Map.Entry<Integer, Integer> entry : kept.entrySet()) {
			int weapon = entry.getKey();
			int slot = entry.getValue();
			if (slot < MAIN_SLOTS) {
				continue;
			}
			Integer last = lastSlot.get(weapon);
			boolean back = last != null && last >= 0 && last < MAIN_SLOTS && (work[last] == EMPTY || work[last] == OTHER && slot == OFFHAND);
			int to = back ? last : firstEmpty(work);
			if (to < 0) {
				changes.add(new Remove(slot));
				work[slot] = EMPTY;
				continue;
			}
			changes.add(new Swap(slot, to));
			work[slot] = work[to];
			work[to] = weapon;
			entry.setValue(to);
		}

		// what Half-Life has and Minecraft lacks
		for (int weapon : owned) {
			if (kept.containsKey(weapon)) {
				continue;
			}
			Integer last = lastSlot.get(weapon);
			int to = last != null && last >= 0 && last < MAIN_SLOTS && work[last] == EMPTY ? last : firstEmpty(work);
			if (to < 0) {
				continue;  // a full inventory: it comes once there's room
			}
			changes.add(new Add(weapon, to));
			work[to] = weapon;
			kept.put(weapon, to);
		}
		return changes;
	}

	/** The first empty main inventory slot (the hotbar comes first), or -1. */
	private static int firstEmpty(int[] work) {
		for (int slot = 0; slot < Math.min(MAIN_SLOTS, work.length); slot++) {
			if (work[slot] == EMPTY) {
				return slot;
			}
		}
		return -1;
	}
}

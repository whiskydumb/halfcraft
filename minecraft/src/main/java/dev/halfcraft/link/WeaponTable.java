package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.JAVA_INT;

import java.lang.foreign.MemorySegment;
import java.lang.invoke.VarHandle;
import java.util.ArrayList;
import java.util.List;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's weapons the player owns, their ammo and the one it has out (WeaponTable in the protocol
 * header), as one consistent read. {@code live} is false while Half-Life has no player in a map: the
 * list says nothing then.
 */
public record WeaponTable(boolean live, int active, List<Weapon> weapons) {
	/** Nothing known: no Half-Life, or none of its players yet. */
	public static final WeaponTable NONE = new WeaponTable(false, HOST_WEAPON_NONE, List.of());

	private static final VarHandle INT = JAVA_INT.varHandle();
	// the last table read, by the link generation and sequence number it was read at
	private static volatile Cached cached = new Cached(-1, 0, NONE);

	private record Cached(int generation, int seq, WeaponTable table) {
	}

	/** One owned weapon. Counts are -1 where the weapon has no such ammo. */
	public record Weapon(int id, int clip, int maxClip, int ammo, int maxAmmo, int ammo2, int maxAmmo2, int flags) {
		public boolean supercharged() {
			return (this.flags & WT_R_SUPERCHARGED) != 0;
		}

		/** Rounds it can fire before reloading: its clip, or its reserve when it has no clip. -1: it uses no ammo. */
		public int ready() {
			return this.clip >= 0 ? this.clip : this.ammo;
		}

		/** What {@link #ready()} holds when full. */
		public int capacity() {
			return this.clip >= 0 ? this.maxClip : this.maxAmmo;
		}

		/** How full {@link #ready()} is, 0-1; -1 for a weapon without ammo. */
		public float fill() {
			if (this.ready() < 0) {
				return -1.0F;
			}
			int capacity = this.capacity();
			return capacity > 0 ? Math.min(1.0F, (float) this.ready() / capacity) : 0.0F;
		}

		/** Half-Life's ammo counter in a line ("18 | 150 | 3"), empty for a weapon without ammo. */
		public String ammoLine() {
			List<String> parts = new ArrayList<>();
			if (this.clip >= 0) {
				parts.add(Integer.toString(this.clip));
			}
			if (this.ammo >= 0) {
				parts.add(Integer.toString(this.ammo));
			}
			if (this.ammo2 >= 0) {
				parts.add(Integer.toString(this.ammo2));
			}
			return String.join(" | ", parts);
		}
	}

	public @Nullable Weapon find(int id) {
		for (Weapon weapon : this.weapons) {
			if (weapon.id() == id) {
				return weapon;
			}
		}
		return null;
	}

	/** The table as Half-Life wrote it last; NONE without a live Half-Life. Any thread. */
	public static WeaponTable current() {
		MemorySegment s = HostLink.segment();
		if (s == null || !HostLink.active()) {
			return NONE;
		}
		int generation = HostLink.generation();
		int seq = (int) INT.getAcquire(s, OFF_WEAPON_TABLE + WT_SEQ);
		Cached last = cached;
		if (last.generation() == generation && last.seq() == seq) {
			return last.table();
		}
		WeaponTable table = read(s, OFF_WEAPON_TABLE);
		if (table == null) {
			// Half-Life is writing it right now: the one before is a frame old at most
			return last.generation() == generation ? last.table() : NONE;
		}
		cached = new Cached(generation, seq, table);
		return table;
	}

	/** Seqlock read of a table at {@code base}; null on a torn read. A table never written reads as NONE. */
	public static @Nullable WeaponTable read(MemorySegment s, long base) {
		for (int attempt = 0; attempt < 16; attempt++) {
			int seq1 = (int) INT.getAcquire(s, base + WT_SEQ);
			if (seq1 == 0) {
				return NONE;
			}
			if ((seq1 & 1) != 0) {
				Thread.onSpinWait();
				continue;
			}
			boolean live = (s.get(JAVA_INT, base + WT_FLAGS) & WT_LIVE) != 0;
			int active = s.get(JAVA_INT, base + WT_ACTIVE);
			int count = Math.clamp(s.get(JAVA_INT, base + WT_COUNT), 0, WT_MAX_WEAPONS);
			List<Weapon> weapons = new ArrayList<>(count);
			for (int i = 0; i < count; i++) {
				long r = base + WT_RECORDS + i * WT_RECORD_BYTES;
				weapons.add(new Weapon(
					s.get(JAVA_INT, r + WT_R_ID), s.get(JAVA_INT, r + WT_R_CLIP), s.get(JAVA_INT, r + WT_R_MAX_CLIP),
					s.get(JAVA_INT, r + WT_R_AMMO), s.get(JAVA_INT, r + WT_R_MAX_AMMO), s.get(JAVA_INT, r + WT_R_AMMO2),
					s.get(JAVA_INT, r + WT_R_MAX_AMMO2), s.get(JAVA_INT, r + WT_R_FLAGS)
				));
			}
			VarHandle.loadLoadFence();
			if ((int) INT.getAcquire(s, base + WT_SEQ) == seq1) {
				return new WeaponTable(live, active, List.copyOf(weapons));
			}
		}
		return null;
	}
}

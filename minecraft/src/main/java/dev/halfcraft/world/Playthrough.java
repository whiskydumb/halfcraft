package dev.halfcraft.world;

import it.unimi.dsi.fastutil.ints.Int2ObjectOpenHashMap;
import it.unimi.dsi.fastutil.ints.IntOpenHashSet;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;

/**
 * A playthrough that began with a new game from Half-Life's menu (see Rollback): the maps it entered,
 * each one's stretch of the mirror world along x, and the chunks of those whose things from before the
 * playthrough are gone already. Each chunk of an entered map is swept once, the first time its things
 * load in the playthrough; what turns up there later is the playthrough's own. Pure state.
 */
final class Playthrough {
	private final IntOpenHashSet slots = new IntOpenHashSet();
	// map slot -> {west, east} (Minecraft x, east exclusive); a slot without them sweeps nothing
	private final Int2ObjectOpenHashMap<int[]> edges = new Int2ObjectOpenHashMap<>();
	private final LongOpenHashSet swept = new LongOpenHashSet();

	/** Whether the playthrough entered the map in this slot already. */
	boolean entered(int slot) {
		return this.slots.contains(slot);
	}

	/** The playthrough enters the map in this slot, which spans x from west to east (exclusive). */
	void enter(int slot, int west, int east) {
		this.slots.add(slot);
		this.edges.put(slot, new int[] { west, east });
	}

	/**
	 * Whether the chunk's things are left from before the playthrough: it's in a map the playthrough
	 * entered and hasn't been swept. It counts as swept from now on.
	 */
	boolean sweep(int chunkX, int chunkZ) {
		long key = key(chunkX, chunkZ);
		if (this.swept.contains(key)) {
			return false;
		}
		int x = chunkX * 16;
		for (int[] span : this.edges.values()) {
			if (x >= span[0] && x < span[1]) {
				this.swept.add(key);
				return true;
			}
		}
		return false;
	}

	/** The chunk was swept some other way (its things were loaded when its map was entered). */
	void markSwept(int chunkX, int chunkZ) {
		this.swept.add(key(chunkX, chunkZ));
	}

	int[] slots() {
		return this.slots.toIntArray();
	}

	/** Each entered map's slot, west and east, one after another. */
	int[] edges() {
		int[] out = new int[this.edges.size() * 3];
		int i = 0;
		for (var entry : this.edges.int2ObjectEntrySet()) {
			out[i++] = entry.getIntKey();
			out[i++] = entry.getValue()[0];
			out[i++] = entry.getValue()[1];
		}
		return out;
	}

	long[] swept() {
		return this.swept.toLongArray();
	}

	/** One from what {@link #slots}, {@link #edges} and {@link #swept} gave. */
	static Playthrough of(int[] slots, int[] edges, long[] swept) {
		Playthrough playthrough = new Playthrough();
		playthrough.slots.addAll(IntOpenHashSet.of(slots));
		for (int i = 0; i + 2 < edges.length; i += 3) {
			playthrough.edges.put(edges[i], new int[] { edges[i + 1], edges[i + 2] });
		}
		playthrough.swept.addAll(LongOpenHashSet.of(swept));
		return playthrough;
	}

	@Override
	public String toString() {
		return "the playthrough has entered map slots " + this.slots;
	}

	private static long key(int chunkX, int chunkZ) {
		return (chunkX & 0xFFFFFFFFL) | (chunkZ & 0xFFFFFFFFL) << 32;
	}
}

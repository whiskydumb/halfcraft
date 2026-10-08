package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class PlaythroughTest {
	@Test
	void shouldSweepAChunkOfAnEnteredMapOnce() {
		Playthrough playthrough = new Playthrough();
		playthrough.enter(6, 5632, 6656);
		assertTrue(playthrough.sweep(384, 12)); // its first load in the playthrough: what's there is from before
		assertFalse(playthrough.sweep(384, 12)); // loaded again: what's there now is the playthrough's own
	}

	@Test
	void shouldLeaveChunksOutsideTheEnteredMapsAlone() {
		Playthrough playthrough = new Playthrough();
		playthrough.enter(6, 5632, 6656);
		assertFalse(playthrough.sweep(5632 / 16 - 1, 0)); // just west of the map's slot
		assertFalse(playthrough.sweep(6656 / 16, 0)); // its east edge is exclusive
		assertTrue(playthrough.sweep(5632 / 16, -40));
	}

	@Test
	void shouldNotSweepAChunkMarkedAlready() {
		Playthrough playthrough = new Playthrough();
		playthrough.enter(7, 6656, 7680);
		playthrough.markSwept(420, 3); // swept when the map was entered, its things loaded then
		assertFalse(playthrough.sweep(420, 3));
	}

	@Test
	void shouldKnowTheMapsItEntered() {
		Playthrough playthrough = new Playthrough();
		playthrough.enter(6, 5632, 6656);
		assertTrue(playthrough.entered(6));
		assertFalse(playthrough.entered(7));
	}

	@Test
	void shouldComeBackTheSameFromItsArrays() {
		Playthrough playthrough = new Playthrough();
		playthrough.enter(6, 5632, 6656);
		playthrough.enter(7, 6656, 7680);
		playthrough.markSwept(384, 12);
		Playthrough copy = Playthrough.of(playthrough.slots(), playthrough.edges(), playthrough.swept());
		assertArrayEquals(playthrough.slots(), copy.slots());
		assertFalse(copy.sweep(384, 12));
		assertTrue(copy.sweep(420, 0));
	}

	@Test
	void shouldSweepNothingInAMapWithoutKnownEdges() {
		// a world from before the edges were kept: its maps are entered, but nothing is known to sweep
		Playthrough playthrough = Playthrough.of(new int[] { 6 }, new int[0], new long[0]);
		assertTrue(playthrough.entered(6));
		assertFalse(playthrough.sweep(384, 12));
	}
}

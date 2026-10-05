package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.HashMap;
import java.util.Map;
import org.junit.jupiter.api.Test;

class HostGroundTest {
	private static final int MAX_DROP = 64;
	private static final double EPS = 1e-9;

	/** Half-Life voxels for a test: cells hold layers, everything in [-16, 16) on each axis is known. */
	private static final class Grid implements NavGrid.Cells {
		private final Map<Long, long[]> cells = new HashMap<>();

		@Override
		public long[] layers(int x, int y, int z) {
			return this.cells.get(key(x, y, z));
		}

		@Override
		public boolean known(int x, int y, int z) {
			return x >= -16 && x < 16 && y >= -16 && y < 16 && z >= -16 && z < 16;
		}

		/** A slab filling cell (x, y, z) up to layer {@code top} (0-7). */
		Grid floor(int x, int y, int z, int top) {
			long[] layers = this.cells.computeIfAbsent(key(x, y, z), k -> new long[8]);
			for (int vy = 0; vy <= top; vy++) {
				layers[vy] = -1L;
			}
			return this;
		}

		private static long key(int x, int y, int z) {
			return ((long) x & 0xFFFF) << 32 | ((long) y & 0xFFFF) << 16 | (long) z & 0xFFFF;
		}
	}

	@Test
	void landsOnFloorInItsOwnCell() {
		Grid grid = new Grid().floor(0, 0, 0, 1);
		assertEquals(0.25, HostGround.landing(grid, 0.5, 0.75, 0.5, MAX_DROP), EPS);
	}

	@Test
	void landsOnTopOfFloorJustAboveTheTarget() {
		Grid grid = new Grid().floor(0, 0, 0, 2);
		assertEquals(0.375, HostGround.landing(grid, 0.5, 0.1, 0.5, MAX_DROP), EPS);
	}

	@Test
	void dropsOntoSolidGroundBelow() {
		Grid grid = new Grid().floor(0, -1, 0, 7);
		assertEquals(0.0, HostGround.landing(grid, 0.5, 3.5, 0.5, MAX_DROP), EPS);
	}

	@Test
	void takesTheFirstGroundOnTheWayDown() {
		Grid grid = new Grid().floor(0, -1, 0, 7).floor(0, 4, 0, 0);
		assertEquals(4.125, HostGround.landing(grid, 0.5, 6.0, 0.5, MAX_DROP), EPS);
	}

	@Test
	void targetInsideGeometryHasNoLanding() {
		Grid grid = new Grid().floor(0, 0, 0, 7);
		assertTrue(Double.isNaN(HostGround.landing(grid, 0.5, 0.5, 0.5, MAX_DROP)));
	}

	@Test
	void nothingBelowHasNoLanding() {
		assertTrue(Double.isNaN(HostGround.landing(new Grid(), 0.5, 3.0, 0.5, MAX_DROP)));
	}

	@Test
	void groundFartherThanTheDropHasNoLanding() {
		Grid grid = new Grid().floor(0, -1, 0, 7);
		assertTrue(Double.isNaN(HostGround.landing(grid, 0.5, 10.0, 0.5, 5)));
	}

	@Test
	void groundOutsideWhatHalfLifeDescribedHasNoLanding() {
		Grid grid = new Grid().floor(0, -18, 0, 7);
		assertTrue(Double.isNaN(HostGround.landing(grid, 0.5, 3.0, 0.5, MAX_DROP)));
	}
}

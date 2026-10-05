package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertSame;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.HashMap;
import java.util.Map;
import org.junit.jupiter.api.Test;

class NavGridTest {
	// a zombie: 0.6 wide, 1.95 tall, steps 0.6 by itself
	private static final double WIDTH = 0.6, HEIGHT = 1.95, STEP = 0.6;
	// a cat: as wide, 0.7 tall, steps as high: nearly all of it is below its step
	private static final double CAT_HEIGHT = 0.7;

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

		/** Fills the voxels x0..x1, y0..y1, z0..z1 (inclusive, 0-7) of cell (x, y, z). */
		Grid fill(int x, int y, int z, int x0, int x1, int y0, int y1, int z0, int z1) {
			long[] layers = this.cells.computeIfAbsent(key(x, y, z), k -> new long[8]);
			for (int vy = y0; vy <= y1; vy++) {
				for (int vz = z0; vz <= z1; vz++) {
					for (int vx = x0; vx <= x1; vx++) {
						layers[vy] |= 1L << (vz * 8 + vx);
					}
				}
			}
			return this;
		}

		/** A floor slab filling the cell up to layer {@code top}. */
		Grid floor(int x, int y, int z, int top) {
			return this.fill(x, y, z, 0, 7, 0, top, 0, 7);
		}

		/** Solid ground under a row of cells x0..x1 at height y (the cells at y - 1 are full). */
		Grid ground(int x0, int x1, int y, int z0, int z1) {
			for (int x = x0; x <= x1; x++) {
				for (int z = z0; z <= z1; z++) {
					this.floor(x, y - 1, z, 7);
				}
			}
			return this;
		}

		private static long key(int x, int y, int z) {
			return ((long) x & 0xFFFF) << 32 | ((long) y & 0xFFFF) << 16 | (long) z & 0xFFFF;
		}
	}

	private static long[] layers(Grid grid, int x, int y, int z) {
		long[] layers = grid.layers(x, y, z);
		return layers != null ? layers : new long[8];
	}

	@Test
	void emptyCellIsEmpty() {
		assertEquals(NavGrid.EMPTY, NavGrid.classify(new long[8]));
	}

	@Test
	void lowSlabIsFloorWithItsTop() {
		int nav = NavGrid.classify(layers(new Grid().floor(0, 0, 0, 2), 0, 0, 0));
		assertEquals(NavGrid.FLOOR, NavGrid.kind(nav));
		assertEquals(2, NavGrid.top(nav));
		assertEquals(0.375, NavGrid.topHeight(nav), 1e-9);
	}

	@Test
	void halfHighSlabIsSolid() {
		int nav = NavGrid.classify(layers(new Grid().floor(0, 0, 0, 3), 0, 0, 0));
		assertEquals(NavGrid.SOLID, NavGrid.kind(nav));
		assertEquals(0.5, NavGrid.topHeight(nav), 1e-9);
	}

	@Test
	void wallsAlongTheEdgesLeaveTheCellEmpty() {
		Grid grid = new Grid().fill(0, 0, 0, 0, 1, 0, 7, 0, 7).fill(0, 0, 0, 6, 7, 0, 7, 0, 7);
		assertEquals(NavGrid.EMPTY, NavGrid.classify(layers(grid, 0, 0, 0)));
	}

	@Test
	void thinPostIsFence() {
		Grid grid = new Grid().fill(0, 0, 0, 3, 3, 0, 7, 3, 3);
		assertEquals(NavGrid.FENCE, NavGrid.kind(NavGrid.classify(layers(grid, 0, 0, 0))));
	}

	@Test
	void railingOnAFloorInTheSameCellIsFence() {
		// floor up to layer 1, a one-voxel railing across the middle up to the top of the cell
		Grid grid = new Grid().floor(0, 0, 0, 1).fill(0, 0, 0, 0, 7, 2, 7, 4, 4);
		assertEquals(NavGrid.FENCE, NavGrid.kind(NavGrid.classify(layers(grid, 0, 0, 0))));
	}

	@Test
	void topOfARailingAloneInItsCellIsFence() {
		// the floor is in the cell below; only the railing's top (layers 0-4) is in this one
		Grid grid = new Grid().fill(0, 0, 0, 0, 7, 0, 4, 4, 4);
		assertEquals(NavGrid.FENCE, NavGrid.kind(NavGrid.classify(layers(grid, 0, 0, 0))));
	}

	@Test
	void slopeRisingIntoOneRowIsSolidNotFence() {
		// a 45-degree shell: middle rows 2, 3, 4, 5 at layers 0, 1, 2, 3
		Grid grid = new Grid();
		for (int row = 0; row < 8; row++) {
			int layer = Math.clamp(row - 2, 0, 7);
			grid.fill(0, 0, 0, 0, 7, layer, layer, row, row);
		}
		assertEquals(NavGrid.SOLID, NavGrid.kind(NavGrid.classify(layers(grid, 0, 0, 0))));
	}

	@Test
	void floorLevelStandsOnFloorsSolidsAndFences() {
		Grid grid = new Grid().floor(0, 0, 0, 2).floor(1, 0, 0, 7).fill(2, 0, 0, 3, 3, 0, 5, 3, 3);
		assertEquals(0.375, NavGrid.floorLevel(grid, 0, 0, 0), 1e-9);
		assertEquals(1.0, NavGrid.floorLevel(grid, 1, 1, 0), 1e-9);
		assertEquals(0.75, NavGrid.floorLevel(grid, 2, 1, 0), 1e-9);
		assertTrue(Double.isNaN(NavGrid.floorLevel(grid, 1, 0, 0)), "nobody stands inside a solid");
		assertTrue(Double.isNaN(NavGrid.floorLevel(grid, 5, 3, 5)), "nothing to stand on");
	}

	@Test
	void standableMeansAFloorOrASolidBelowButNotAFence() {
		Grid grid = new Grid().floor(0, 0, 0, 1).floor(1, 0, 0, 7).fill(2, 0, 0, 3, 3, 0, 7, 3, 3);
		assertTrue(NavGrid.standable(grid, 0, 0, 0));
		assertTrue(NavGrid.standable(grid, 1, 1, 0));
		assertFalse(NavGrid.standable(grid, 2, 1, 0));
		assertFalse(NavGrid.standable(grid, 1, 0, 0));
		assertFalse(NavGrid.standable(grid, 3, 1, 0));
	}

	@Test
	void standingYFindsTheGroundBelowWithinReach() {
		Grid grid = new Grid().floor(0, 0, 0, 7).floor(1, 0, 0, 1);
		assertEquals(1, NavGrid.standingY(grid, 0, 5, 0, 10));
		assertEquals(0, NavGrid.standingY(grid, 1, 5, 0, 10));
		assertEquals(1, NavGrid.standingY(grid, 0, 0, 0, 10), "inside a solid: on top of it");
		assertEquals(Integer.MIN_VALUE, NavGrid.standingY(grid, 0, 5, 0, 3), "too far down");
		assertEquals(Integer.MIN_VALUE, NavGrid.standingY(grid, 5, 5, 5, 10), "no ground at all");
	}

	@Test
	void roofedLooksUpAsFarAsKnown() {
		Grid grid = new Grid().floor(0, 4, 0, 0);
		assertTrue(NavGrid.roofed(grid, 0, 1, 0));
		assertFalse(NavGrid.roofed(grid, 1, 1, 0));
		assertFalse(NavGrid.roofed(grid, 0, 5, 0), "the roof is below");
	}

	@Test
	void boxHitsOverlapsButNotTouches() {
		Grid grid = new Grid().fill(0, 0, 0, 4, 7, 0, 7, 0, 7);
		assertTrue(NavGrid.boxHits(grid, 0.4, 0.1, 0.1, 0.6, 0.9, 0.9));
		assertFalse(NavGrid.boxHits(grid, 0.1, 0.1, 0.1, 0.5, 0.9, 0.9), "the box only touches x = 0.5");
		assertFalse(NavGrid.boxHits(grid, 1.1, 0.1, 0.1, 1.9, 0.9, 0.9));
	}

	@Test
	void spotIsTheMiddleWhenItFits() {
		Grid grid = new Grid().ground(0, 0, 0, 0, 0);
		assertSame(NavGrid.MIDDLE_SPOT, NavGrid.spot(grid, 0.5, 0.0, 0.5, WIDTH, HEIGHT, STEP));
	}

	@Test
	void spotStepsAwayFromAWallAtTheEdge() {
		// a wall in voxels 6-7 (x 0.75-1.0) over the whole height: the middle overlaps it by 0.05
		Grid grid = new Grid().ground(0, 0, 0, 0, 0).fill(0, 0, 0, 6, 7, 0, 7, 0, 7).fill(0, 1, 0, 6, 7, 0, 7, 0, 7);
		double[] spot = NavGrid.spot(grid, 0.5, 0.0, 0.5, WIDTH, HEIGHT, STEP);
		assertNotNull(spot);
		assertArrayEquals(new double[] { -0.125, 0.0 }, spot, 1e-9);
	}

	@Test
	void spotIsNullWhenNothingNearFits() {
		Grid grid = new Grid().ground(0, 0, 0, 0, 0).fill(0, 0, 0, 0, 7, 6, 7, 0, 7).fill(0, 1, 0, 0, 7, 0, 7, 0, 7);
		assertNull(NavGrid.spot(grid, 0.5, 0.0, 0.5, WIDTH, HEIGHT, STEP));
	}

	@Test
	void crossesOpenGround() {
		Grid grid = new Grid().ground(0, 1, 0, 0, 0);
		assertTrue(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 0.5, 0.0, WIDTH, HEIGHT, STEP));
	}

	@Test
	void wallBetweenTwoEmptyCellsStopsTheStep() {
		// a wall straddling x = 1 (voxels 6-7 of cell 0 and 0-1 of cell 1): both cells classify empty
		Grid grid = new Grid().ground(0, 1, 0, 0, 0);
		for (int y = 0; y < 2; y++) {
			grid.fill(0, y, 0, 6, 7, 0, 7, 0, 7).fill(1, y, 0, 0, 1, 0, 7, 0, 7);
		}
		assertEquals(NavGrid.EMPTY, NavGrid.kind(grid.nav(0, 0, 0)));
		assertEquals(NavGrid.EMPTY, NavGrid.kind(grid.nav(1, 0, 0)));
		assertFalse(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 0.5, 0.0, WIDTH, HEIGHT, STEP));
	}

	@Test
	void wallAlongsideLetsTheStepThroughOffTheMiddle() {
		// a wall along the z edge (voxels 6-7 in z) of both cells: the mob walks a little off the middle
		Grid grid = new Grid().ground(0, 1, 0, 0, 0);
		for (int x = 0; x < 2; x++) {
			for (int y = 0; y < 2; y++) {
				grid.fill(x, y, 0, 0, 7, 0, 7, 6, 7);
			}
		}
		assertTrue(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 0.5, 0.0, WIDTH, HEIGHT, STEP));
	}

	@Test
	void stairRisersDontCountAsWalls() {
		// cell 1 holds a half-block step: its riser is below the height a mob steps up by itself
		Grid grid = new Grid().ground(0, 1, 0, 0, 0).floor(1, 0, 0, 3);
		assertTrue(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 0.5, 0.5, WIDTH, HEIGHT, STEP));
	}

	@Test
	void shortMobStepsAwayFromAWallAtTheEdgeToo() {
		Grid grid = new Grid().ground(0, 0, 0, 0, 0).fill(0, 0, 0, 6, 7, 0, 7, 0, 7);
		double[] spot = NavGrid.spot(grid, 0.5, 0.0, 0.5, WIDTH, CAT_HEIGHT, STEP);
		assertNotNull(spot);
		assertArrayEquals(new double[] { -0.125, 0.0 }, spot, 1e-9);
	}

	@Test
	void wallBetweenTwoEmptyCellsStopsAShortMob() {
		Grid grid = new Grid().ground(0, 1, 0, 0, 0);
		grid.fill(0, 0, 0, 6, 7, 0, 7, 0, 7).fill(1, 0, 0, 0, 1, 0, 7, 0, 7);
		assertFalse(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 0.5, 0.0, WIDTH, CAT_HEIGHT, STEP));
	}

	@Test
	void shortMobStillClimbsAStairItSteps() {
		Grid grid = new Grid().ground(0, 1, 0, 0, 0).floor(1, 0, 0, 3);
		assertTrue(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 0.5, 0.5, WIDTH, CAT_HEIGHT, STEP));
	}

	@Test
	void lowCeilingOverTheHigherFloorStopsTheStep() {
		// cell 1 is a half-block step with a ceiling 1.5 above it: a zombie (1.95) doesn't fit under it
		Grid grid = new Grid().ground(0, 1, 0, 0, 0).floor(1, 0, 0, 3).fill(1, 2, 0, 0, 7, 0, 7, 0, 7);
		assertFalse(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 0.5, 0.5, WIDTH, HEIGHT, STEP));
	}

	@Test
	void diagonalPastAWallCornerIsStopped() {
		// a post at the corner where cells (0, 0), (1, 0), (0, 1) and (1, 1) meet
		Grid grid = new Grid().ground(0, 1, 0, 0, 1);
		for (int y = 0; y < 2; y++) {
			grid.fill(0, y, 0, 7, 7, 0, 7, 7, 7).fill(1, y, 1, 0, 0, 0, 7, 0, 0);
		}
		assertFalse(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 1.5, 0.0, WIDTH, HEIGHT, STEP));
		assertTrue(NavGrid.canCross(grid, 0.5, 0.5, 0.0, 1.5, 0.5, 0.0, WIDTH, HEIGHT, STEP));
	}
}

package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class FluidCellsTest {
	private static final long ALL = -1L;

	/** Voxel layers filled from layer {@code lo} up to {@code hi} (inclusive) over the whole cell. */
	private static long[] solid(int lo, int hi) {
		long[] layers = new long[8];
		for (int y = lo; y <= hi; y++) {
			layers[y] = ALL;
		}
		return layers;
	}

	/** A wall along the cell's west edge (x 0..1), floor to top. */
	private static long[] westWall() {
		long[] layers = new long[8];
		for (int y = 0; y < 8; y++) {
			for (int z = 0; z < 8; z++) {
				layers[y] |= 3L << (z * 8);
			}
		}
		return layers;
	}

	@Test
	void shouldSeeGeometryOnlyThroughTheMiddle() {
		assertFalse(FluidCells.middleBlocked(westWall(), 0.0, 1.0));
		assertTrue(FluidCells.middleBlocked(solid(2, 3), 0.0, 1.0));
		assertFalse(FluidCells.middleBlocked(solid(2, 3), 0.5, 1.0));
		assertFalse(FluidCells.middleBlocked(null, 0.0, 1.0));
	}

	@Test
	void shouldLetAFluidFallPastAWallInItsCell() {
		// water put by a wall in mid-air: nothing under its middle, an empty cell below
		assertNull(FluidCells.down(1.0F, 0.0F, westWall(), 0.0F, null));
	}

	@Test
	void shouldKeepAFluidOnItsFloor() {
		assertEquals("resting on ground", FluidCells.down(1.0F, 0.25F, solid(0, 1), 0.0F, solid(0, 7)));
		// a floor right at the cell's bottom: its top layer is in the cell below
		assertEquals("resting on ground", FluidCells.down(1.0F, 0.0F, null, 0.95F, solid(0, 7)));
		// a steep slope through the middle, no floor to walk on: still not through it
		assertEquals("resting on ground", FluidCells.down(1.0F, 0.0F, solid(2, 2), 0.0F, null));
	}

	@Test
	void shouldNotMindACeilingAboveItsSurface() {
		// a ceiling in the top layer of a shallow fluid's cell
		assertNull(FluidCells.down(0.5F, 0.0F, solid(7, 7), 0.0F, null));
	}

	@Test
	void shouldNotFallOntoGroundUpToWhereItWouldLand() {
		assertEquals("ground below", FluidCells.down(1.0F, 0.0F, null, 0.88F, solid(6, 6)));
		assertNull(FluidCells.down(1.0F, 0.0F, null, 0.5F, solid(3, 3)));
	}

	@Test
	void shouldFlowIntoACellAWallCrossesTheFarSideOf() {
		assertNull(FluidCells.sideways(7.0F / 9.0F, 0.0F, 0.0F, westWall(), null, true));
	}

	@Test
	void shouldNotFlowPastAWallToTheMiddle() {
		// a wall between the fluid and the middle, or a brush the middle is inside of
		assertEquals("wall beside", FluidCells.sideways(7.0F / 9.0F, 0.0F, 0.0F, westWall(), null, false));
	}

	@Test
	void shouldNotFlowUpAStepAboveItsSurface() {
		// a step of a third of a block, its top walkable, against a fluid of a ninth
		assertEquals("ground beside", FluidCells.sideways(1.0F / 9.0F, 0.0F, 0.375F, solid(0, 2), null, true));
	}

	@Test
	void shouldFlowOntoLevelGround() {
		assertNull(FluidCells.sideways(0.5F, 0.85F, 0.9F, solid(7, 7), null, true));
	}

	@Test
	void shouldNotFlowUnderTheGround() {
		assertEquals("under ground", FluidCells.sideways(0.8F, 0.0F, 0.0F, null, solid(0, 7), true));
		// a thick brush has only its surfaces: under its floor high in the cell above, the empty cell is inside it
		assertEquals("under ground", FluidCells.sideways(0.8F, 0.0F, 0.0F, null, solid(3, 3), true));
		assertNull(FluidCells.sideways(0.8F, 0.0F, 0.0F, null, null, true));
	}
}

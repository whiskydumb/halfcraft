package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

/**
 * The host shifts each map's block grid onto its most common floor (source/src/core/hc_grid.h). Minecraft
 * never sees the shift, only its coordinates, but the shift is worked out from this side's placement rule
 * ({@link HostRay#placementCell}) on floors that arrive as floats: these tests hold the host's rule against it.
 */
class GridOffsetTest {
	private static final int UNITS_PER_BLOCK = 40;
	/** A floor this far over a grid line sinks or floats depending on how its height rounds to a float. */
	private static final int COIN_FLIP_RESIDUE = 36;

	/** The host's {@code grid_z_for_floor}: the grid height (units) that puts a floor (units) on a grid line. */
	private static int gridZForFloor(int floorZ) {
		int r = Math.floorMod(floorZ, UNITS_PER_BLOCK);
		return r <= COIN_FLIP_RESIDUE ? r : r - UNITS_PER_BLOCK;
	}

	private static List<HostTri> ground(float y) {
		float[] a = { -10, y, -10, 10, y, -10, 10, y, 10 };
		float[] b = { -10, y, -10, 10, y, 10, -10, y, 10 };
		List<HostTri> out = new ArrayList<>();
		out.add(new HostTri(a, 0, false));
		out.add(new HostTri(b, 0, false));
		return out;
	}

	/** Where minecraft places a block on a floor at source height floorZ with the grid at gridZ: its bottom, in source units. */
	private static int placedBottom(int floorZ, int gridZ) {
		// the host sends the floor as a float
		float y = (float) ((floorZ - gridZ) / (double) UNITS_PER_BLOCK);
		HostRay.Hit hit = HostRay.cast(ground(y), 0.5, y + 2.0, 0.5, 0.5, y - 2.0, 0.5);
		return HostRay.placementCell(hit)[1] * UNITS_PER_BLOCK + gridZ;
	}

	@Test
	void hostRuleUsesThePlacementGap() {
		// hc_grid.h's PLACEMENT_GAP_UNITS
		assertEquals(4.0, HostRay.PLACEMENT_GAP * UNITS_PER_BLOCK, 1e-9);
	}

	@Test
	void blocksPlacedOnTheGridFloorSitFlush() {
		for (int floorZ = -4000; floorZ < 4000; floorZ++) {
			assertEquals(floorZ, placedBottom(floorZ, gridZForFloor(floorZ)), "floor at " + floorZ);
		}
	}

	@Test
	void blocksPlacedBeforeTheShiftEndUpFlush() {
		// a block placed while the grid was at 0 keeps its cell; the shift moves it by gridZ
		for (int floorZ = -4000; floorZ < 4000; floorZ++) {
			if (Math.floorMod(floorZ, UNITS_PER_BLOCK) != COIN_FLIP_RESIDUE) {
				assertEquals(floorZ, placedBottom(floorZ, 0) + gridZForFloor(floorZ), "floor at " + floorZ);
			}
		}
	}

	@Test
	void shiftIsNeverAWholeBlock() {
		for (int floorZ = -4000; floorZ < 4000; floorZ++) {
			int gridZ = gridZForFloor(floorZ);
			assertTrue(gridZ >= -3 && gridZ <= 36, "floor at " + floorZ + " shifts " + gridZ);
		}
	}

	@Test
	void workedExamplesFromTheHost() {
		// d1_trainstation_01's station floor: sunk 8 before, the grid goes up 8
		assertEquals(-40, placedBottom(-32, 0));
		assertEquals(8, gridZForFloor(-32));
		// 2 under a grid line: floated 2 before, the grid comes down 2
		assertEquals(80, placedBottom(78, 0));
		assertEquals(-2, gridZForFloor(78));
		// just above a grid line: up 2, not down 38
		assertEquals(2, gridZForFloor(2));
		assertEquals(-2, gridZForFloor(-2));
	}

	@Test
	void coinFlipResidueGoesUp() {
		// 1.9 is a hair under as a float (the block sank 36), 2.9 a hair over (it floated 4)
		assertEquals(40, placedBottom(76, 0));
		assertEquals(120, placedBottom(116, 0));
		// either way the grid goes up 36: the one that sank ends up flush, the other floats a block
		assertEquals(36, gridZForFloor(76));
		assertEquals(36, gridZForFloor(116));
		assertEquals(76, placedBottom(76, 0) + gridZForFloor(76));
		assertEquals(116 + UNITS_PER_BLOCK, placedBottom(116, 0) + gridZForFloor(116));
	}
}

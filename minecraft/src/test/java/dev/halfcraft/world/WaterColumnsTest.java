package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Arrays;
import java.util.List;
import org.junit.jupiter.api.Test;

class WaterColumnsTest {
	private static final float NONE = -1.0E30F;

	/** A player's grid of the given size whose every column has water at {@code surface} (or none). */
	private static WaterColumns.Grid grid(int originX, int originZ, int size, float surface) {
		return grid(originX, originZ, size, surface, Double.NEGATIVE_INFINITY);
	}

	/** A grid whose search went down to {@code bottom}, every column with water at {@code surface} (or none). */
	private static WaterColumns.Grid grid(int originX, int originZ, int size, float surface, double bottom) {
		float[] s = new float[size * size];
		Arrays.fill(s, surface);
		return new WaterColumns.Grid(originX, originZ, size, s, bottom);
	}

	@Test
	void shouldReadTheColumnInsideAGrid() {
		WaterColumns.Grid g = grid(10, 20, 4, NONE);
		g.surface()[2 * 4 + 1] = 5.5F;
		assertEquals(5.5, WaterColumns.surfaceIn(g, 11, 22), 1e-6);
		assertTrue(Double.isNaN(WaterColumns.surfaceIn(g, 10, 20)));
	}

	@Test
	void shouldReturnNanOutsideAGrid() {
		WaterColumns.Grid g = grid(10, 20, 4, 5.0F);
		assertTrue(Double.isNaN(WaterColumns.surfaceIn(g, 14, 20)));
		assertTrue(Double.isNaN(WaterColumns.surfaceIn(g, 9, 21)));
		assertTrue(Double.isNaN(WaterColumns.surfaceIn(null, 11, 21)));
	}

	@Test
	void shouldPreferThePlayersGridAndFallBackToProbes() {
		WaterColumns.Grid player = grid(0, 0, 16, 6.0F);
		WaterColumns.Grid probe = grid(12, 12, 8, 2.0F, -4.0);
		assertEquals(6.0, WaterColumns.surface(player, List.of(probe), 13, 1, 13), 1e-6);
		assertEquals(2.0, WaterColumns.surface(player, List.of(probe), 18, 1, 18), 1e-6);
		assertTrue(Double.isNaN(WaterColumns.surface(player, List.of(probe), 30, 1, 30)));
	}

	@Test
	void shouldTakeAProbesWaterWhereThePlayersGridFoundNone() {
		// a bobber cast down into a canal below the player's window: the player's grid says none there
		WaterColumns.Grid player = grid(0, 0, 16, NONE);
		WaterColumns.Grid probe = grid(4, 4, 8, -8.0F, -14.0);
		assertEquals(-8.0, WaterColumns.surface(player, List.of(probe), 6, -9, 6), 1e-6);
	}

	@Test
	void shouldTakeNoWaterFromAProbeUnderWhereItSearched() {
		// a bobber in a raised pool: its probe found the surface at 10 and searched down to 4; the
		// player below the pool has none in its own grid, and the air under the pool isn't water
		WaterColumns.Grid player = grid(0, 0, 16, NONE);
		WaterColumns.Grid probe = grid(4, 4, 8, 10.0F, 4.0);
		assertEquals(10.0, WaterColumns.surface(player, List.of(probe), 6, 4, 6), 1e-6);
		assertTrue(Double.isNaN(WaterColumns.surface(player, List.of(probe), 6, 3, 6)));
		assertTrue(Double.isNaN(WaterColumns.surface(player, List.of(probe), 6, -2, 6)));
	}

	@Test
	void shouldLetThePlayersGridReachEveryCellBelowItsSurface() {
		WaterColumns.Grid player = grid(0, 0, 16, 6.0F);
		assertEquals(6.0, WaterColumns.surface(player, List.of(), 3, -100, 3), 1e-6);
	}

	@Test
	void shouldChooseTheNearestFreshWantsUpToTheLimit() {
		List<WaterColumns.Want> wants = List.of(
			new WaterColumns.Want(1, 0, 30.0, 1.0, 0.0, 1000),
			new WaterColumns.Want(2, 0, 5.0, 2.0, 0.0, 1000),
			new WaterColumns.Want(3, 0, 1.0, 3.0, 0.0, 0), // asked too long ago
			new WaterColumns.Want(4, 0, -10.0, 4.0, 0.0, 1000)
		);
		List<WaterColumns.Want> chosen = WaterColumns.choose(wants, 0.0, 0.0, 1500, 1000, 2);
		assertEquals(2, chosen.size());
		assertEquals(2, chosen.get(0).id());
		assertEquals(4, chosen.get(1).id());
	}

	@Test
	void shouldChooseLowerRanksBeforeNearerOnes() {
		List<WaterColumns.Want> wants = List.of(
			new WaterColumns.Want(1, 2, 1.0, 0.0, 0.0, 1000), // an item right by the grid's middle
			new WaterColumns.Want(2, 0, 40.0, 0.0, 0.0, 1000), // a boat far off
			new WaterColumns.Want(3, 1, 20.0, 0.0, 0.0, 1000), // a mob
			new WaterColumns.Want(4, 0, 30.0, 0.0, 0.0, 1000) // a nearer boat
		);
		List<WaterColumns.Want> chosen = WaterColumns.choose(wants, 0.0, 0.0, 1500, 1000, 3);
		assertEquals(List.of(4, 2, 3), chosen.stream().map(WaterColumns.Want::id).toList());
	}

	@Test
	void shouldKnowTheCellsAGridSearched() {
		WaterColumns.Grid player = grid(0, 0, 16, NONE);
		WaterColumns.Grid probe = grid(40, 0, 8, 5.0F, 0.0);
		assertTrue(WaterColumns.known(player, List.of(probe), 3, -100, 3)); // the player's grid reaches every cell
		assertTrue(WaterColumns.known(player, List.of(probe), 42, 2, 3));
		assertFalse(WaterColumns.known(player, List.of(probe), 42, -5, 3)); // under where the probe looked
		assertFalse(WaterColumns.known(player, List.of(probe), 60, 2, 3)); // no grid there
		assertFalse(WaterColumns.known(null, List.of(), 3, 2, 3));
	}
}

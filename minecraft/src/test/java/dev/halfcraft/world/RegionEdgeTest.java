package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class RegionEdgeTest {
	private static final int SIZE = 8;

	/** Regions -5..4 along x and z and -3..2 along y are known, like the volume streamed around a player at the origin. */
	private static final RegionEdge.Known VOLUME = (rx, ry, rz) -> rx >= -5 && rx <= 4 && rz >= -5 && rz <= 4 && ry >= -3 && ry <= 2;

	@Test
	void shouldReturnNullWhenSegmentStaysInKnownRegions() {
		assertNull(RegionEdge.first(1.0, 2.0, 3.0, 30.0, 2.0, 3.0, SIZE, VOLUME));
	}

	@Test
	void shouldStopAtTheFaceOfTheFirstUnknownRegionAlongX() {
		// x 38 -> 42 crosses into region 5 at x = 40
		RegionEdge.Crossing edge = RegionEdge.first(38.0, 2.0, 3.0, 42.0, 2.0, 3.0, SIZE, VOLUME);
		assertNotNull(edge);
		assertEquals(0.5, edge.t(), 1e-9);
		assertEquals(0, edge.axis());
		assertEquals(1, edge.step());
	}

	@Test
	void shouldStopGoingDownThroughTheBottomOfTheVolume() {
		// y -23 -> -26 leaves region -3 at y = -24
		RegionEdge.Crossing edge = RegionEdge.first(0.5, -23.0, 0.5, 0.5, -26.0, 0.5, SIZE, VOLUME);
		assertNotNull(edge);
		assertEquals(1.0 / 3.0, edge.t(), 1e-9);
		assertEquals(1, edge.axis());
		assertEquals(-1, edge.step());
	}

	@Test
	void shouldStopAtTheNearerFaceOnADiagonal() {
		// leaves through z = -40 (t 0.25) before reaching x = 40 (t 0.5)
		RegionEdge.Crossing edge = RegionEdge.first(38.0, 0.0, -39.0, 42.0, 0.0, -43.0, SIZE, VOLUME);
		assertNotNull(edge);
		assertEquals(0.25, edge.t(), 1e-9);
		assertEquals(2, edge.axis());
		assertEquals(-1, edge.step());
	}

	@Test
	void shouldPassKnownRegionsBeforeAnUnknownOne() {
		// a known volume with a hole in region (2, 0, 0): x 10 -> 26 crosses region 1, then stops at x = 16
		RegionEdge.Known holed = (rx, ry, rz) -> !(rx == 2 && ry == 0 && rz == 0);
		RegionEdge.Crossing edge = RegionEdge.first(10.0, 1.0, 1.0, 26.0, 1.0, 1.0, SIZE, holed);
		assertNotNull(edge);
		assertEquals(6.0 / 16.0, edge.t(), 1e-9);
	}

	@Test
	void shouldReturnNullWhenStartingInAnUnknownRegion() {
		assertNull(RegionEdge.first(50.0, 0.0, 0.0, 60.0, 0.0, 0.0, SIZE, VOLUME));
	}

	@Test
	void shouldCrossAtOnceFromAPointOnTheFaceGoingOut() {
		// standing exactly on the volume's face at x = -40 (the edge of known region -5), going out crosses it at once
		RegionEdge.Crossing edge = RegionEdge.first(-40.0, 0.0, 0.0, -41.0, 0.0, 0.0, SIZE, VOLUME);
		assertNotNull(edge);
		assertEquals(0.0, edge.t(), 1e-9);
		assertEquals(-1, edge.step());
	}

	@Test
	void shouldReturnNullForAStillSegment() {
		assertNull(RegionEdge.first(1.0, 1.0, 1.0, 1.0, 1.0, 1.0, SIZE, VOLUME));
	}

	@Test
	void shouldLetAShotOutOfTheTopFlyOnOverKnownGround() {
		// y 22 -> 26 leaves region 2 at y = 24; the sky over the volume is open
		assertNull(RegionEdge.first(0.5, 22.0, 0.5, 0.5, 26.0, 0.5, SIZE, RegionEdge.withSky(VOLUME, 4)));
	}

	@Test
	void shouldStopAShotInTheSkyWhereNothingBelowIsKnown() {
		// at y 30 (region 3, over the volume) x 38 -> 42 goes on into column 5, which has nothing known
		RegionEdge.Crossing edge = RegionEdge.first(38.0, 30.0, 3.0, 42.0, 30.0, 3.0, SIZE, RegionEdge.withSky(VOLUME, 4));
		assertNotNull(edge);
		assertEquals(0, edge.axis());
		assertEquals(0.5, edge.t(), 1e-9);
	}

	@Test
	void shouldStillStopAtTheBottomWithTheSkyOpen() {
		RegionEdge.Crossing edge = RegionEdge.first(0.5, -23.0, 0.5, 0.5, -26.0, 0.5, SIZE, RegionEdge.withSky(VOLUME, 4));
		assertNotNull(edge);
		assertEquals(1, edge.axis());
		assertEquals(-1, edge.step());
	}

	@Test
	void shouldOpenTheSkyOnlyAsHighAsAsked() {
		RegionEdge.Known sky = RegionEdge.withSky(VOLUME, 4);
		assertTrue(sky.region(0, 6, 0));
		assertFalse(sky.region(0, 7, 0));
	}
}

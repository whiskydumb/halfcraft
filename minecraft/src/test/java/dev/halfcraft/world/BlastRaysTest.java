package dev.halfcraft.world;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class BlastRaysTest {
	private static HostTri tri(boolean stairHelper, double... v) {
		float[] f = new float[9];
		for (int i = 0; i < 9; i++) {
			f[i] = (float) v[i];
		}
		return new HostTri(f, 0, stairHelper);
	}

	/** A floor at y 0 and a wall in the plane x = 2, both spanning [-10, 10]. */
	private static List<HostTri> floorAndWall(boolean wallIsStairHelper) {
		List<HostTri> out = new ArrayList<>();
		out.add(tri(false, -10, 0, -10, 10, 0, -10, 10, 0, 10));
		out.add(tri(false, -10, 0, -10, 10, 0, 10, -10, 0, 10));
		out.add(tri(wallIsStairHelper, 2, -10, -10, 2, 10, -10, 2, 10, 10));
		out.add(tri(wallIsStairHelper, 2, -10, -10, 2, 10, 10, 2, -10, 10));
		return out;
	}

	@Test
	void shouldHideABodyBehindAWall() {
		assertTrue(BlastRays.hidden(floorAndWall(false), 0, 0, 0.5, 4, 1, 0.5));
	}

	@Test
	void shouldSeeABodyOnTheSameFloorFromACreeperStandingOnIt() {
		// the creeper's centre and the body's feet both lie in the floor's plane
		assertFalse(BlastRays.hidden(floorAndWall(false), 0, 0, 0.5, 1.5, 0, 0.5));
		assertFalse(BlastRays.hidden(floorAndWall(false), 0, 0, 0.5, 1.5, 1.8, 0.5));
	}

	@Test
	void shouldHideABodyBelowTheFloor() {
		assertTrue(BlastRays.hidden(floorAndWall(false), 0, 0, 0.5, 0, -2, 0.5));
	}

	@Test
	void shouldIgnoreStairHelpers() {
		assertFalse(BlastRays.hidden(floorAndWall(true), 0, 0, 0.5, 4, 1, 0.5));
	}

	@Test
	void shouldStopABlockRayAtTheWall() {
		assertEquals(2.0, BlastRays.reach(floorAndWall(false), 0, 1, 0.5, 1, 0, 0, 7), 1e-6);
	}

	@Test
	void shouldStopADownwardRayAtTheFloorAndLetAnUpwardOneGo() {
		assertEquals(BlastRays.LIFT, BlastRays.reach(floorAndWall(false), 0, 0, 0.5, 0, -1, 0, 7), 1e-6);
		assertEquals(7.0, BlastRays.reach(floorAndWall(false), 0, 0, 0.5, 0, 1, 0, 7), 0.0);
	}
}

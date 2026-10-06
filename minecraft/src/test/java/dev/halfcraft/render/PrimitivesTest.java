package dev.halfcraft.render;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;

import org.junit.jupiter.api.Test;

class PrimitivesTest {
	@Test
	void shouldKeepQuadsAsTheyAre() {
		assertArrayEquals(new int[] { 0, 1, 2, 3, 4, 5, 6, 7 }, Primitives.quads(Primitives.Kind.QUADS, 9));
	}

	@Test
	void shouldDoubleEachTrianglesLastCorner() {
		assertArrayEquals(new int[] { 0, 1, 2, 2, 3, 4, 5, 5 }, Primitives.quads(Primitives.Kind.TRIANGLES, 7));
	}

	@Test
	void shouldTakeAStripTwoVerticesAtATime() {
		// a leash's strip: pairs (0, 1), (2, 3), (4, 5) make two quads
		assertArrayEquals(new int[] { 0, 1, 3, 2, 2, 3, 5, 4 }, Primitives.quads(Primitives.Kind.TRIANGLE_STRIP, 6));
		assertArrayEquals(new int[0], Primitives.quads(Primitives.Kind.TRIANGLE_STRIP, 1));
	}

	@Test
	void shouldFanOutFromTheFirstVertex() {
		assertArrayEquals(new int[] { 0, 1, 2, 2, 0, 2, 3, 3 }, Primitives.quads(Primitives.Kind.TRIANGLE_FAN, 4));
	}

	@Test
	void shouldMakeNoQuadsOfLinesAndNoLinesOfSurfaces() {
		assertNull(Primitives.quads(Primitives.Kind.LINES, 4));
		assertNull(Primitives.segments(Primitives.Kind.QUADS, 4));
	}

	@Test
	void shouldPairLinesAndChainAStrip() {
		assertArrayEquals(new int[] { 0, 1, 2, 3 }, Primitives.segments(Primitives.Kind.LINES, 5));
		assertArrayEquals(new int[] { 0, 1, 1, 2, 2, 3 }, Primitives.segments(Primitives.Kind.LINE_STRIP, 4));
	}

	@Test
	void shouldMakeARibbonAcrossTheViewWidenedWithDistance() {
		// a line along x, 2 blocks in front of an eye that looks along -z at it
		float[] corners = Primitives.ribbon(new float[] { 0, 0, -2 }, new float[] { 1, 0, -4 }, new float[] { 0, 0, 0 }, 0.01F);
		assertNotNull(corners);
		// at a (2 blocks away) the ribbon is 0.02 wide, at b (sqrt 17 away) wider
		float widthA = distance(corners, 0, 1);
		float widthB = distance(corners, 2, 3);
		assertEquals(0.02F, widthA, 1e-5F);
		assertEquals(0.01F * (float) Math.sqrt(17.0), widthB, 1e-5F);
		// the ribbon's middle is the line itself
		assertEquals(0.0F, (corners[0] + corners[3]) / 2.0F, 1e-6F);
		assertEquals(-2.0F, (corners[2] + corners[5]) / 2.0F, 1e-6F);
		// and it lies across the view: at a, its side is square to the way to the eye
		float sx = corners[3] - corners[0], sy = corners[4] - corners[1], sz = corners[5] - corners[2];
		assertEquals(0.0F, sx * 0 + sy * 0 + sz * -2, 1e-6F);
	}

	@Test
	void shouldMakeNoRibbonLookedAtEndOn() {
		assertNull(Primitives.ribbon(new float[] { 0, 0, -2 }, new float[] { 0, 0, -4 }, new float[] { 0, 0, 0 }, 0.01F));
	}

	private static float distance(float[] corners, int i, int j) {
		float dx = corners[j * 3] - corners[i * 3], dy = corners[j * 3 + 1] - corners[i * 3 + 1], dz = corners[j * 3 + 2] - corners[i * 3 + 2];
		return (float) Math.sqrt(dx * dx + dy * dy + dz * dz);
	}
}

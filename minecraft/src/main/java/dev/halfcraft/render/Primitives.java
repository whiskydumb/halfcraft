package dev.halfcraft.render;

import org.jspecify.annotations.Nullable;

/**
 * Minecraft's geometry that isn't quads (lines, strips, fans, triangles) as the quads Half-Life's
 * entity batches take. Pure logic: the client's AvatarExporter collects what renderers submit as
 * custom geometry (the fishing line and bobber) and leashes, and turns it into quads with this.
 */
public final class Primitives {
	private Primitives() {
	}

	/** How a run of vertices makes primitives (Minecraft's PrimitiveTopology, less points). */
	public enum Kind {
		QUADS, TRIANGLES, TRIANGLE_STRIP, TRIANGLE_FAN, LINES, LINE_STRIP
	}

	/**
	 * The quads, four vertex indices each, that cover what {@code count} vertices of a surface make. A
	 * triangle becomes a quad with its last corner twice; a strip is taken two vertices at a time
	 * (its two triangles there are one quad); null for lines.
	 */
	public static int @Nullable [] quads(Kind kind, int count) {
		return switch (kind) {
			case QUADS -> range(count / 4 * 4);
			case TRIANGLES -> {
				int[] out = new int[count / 3 * 4];
				for (int t = 0; t < count / 3; t++) {
					out[t * 4] = t * 3;
					out[t * 4 + 1] = t * 3 + 1;
					out[t * 4 + 2] = t * 3 + 2;
					out[t * 4 + 3] = t * 3 + 2;
				}
				yield out;
			}
			case TRIANGLE_STRIP -> {
				int pairs = Math.max(0, count / 2 - 1);
				int[] out = new int[pairs * 4];
				for (int p = 0; p < pairs; p++) {
					out[p * 4] = p * 2;
					out[p * 4 + 1] = p * 2 + 1;
					out[p * 4 + 2] = p * 2 + 3;
					out[p * 4 + 3] = p * 2 + 2;
				}
				yield out;
			}
			case TRIANGLE_FAN -> {
				int triangles = Math.max(0, count - 2);
				int[] out = new int[triangles * 4];
				for (int t = 0; t < triangles; t++) {
					out[t * 4] = 0;
					out[t * 4 + 1] = t + 1;
					out[t * 4 + 2] = t + 2;
					out[t * 4 + 3] = t + 2;
				}
				yield out;
			}
			case LINES, LINE_STRIP -> null;
		};
	}

	/** The lines, two vertex indices each, that {@code count} vertices of a line kind make; null for surfaces. */
	public static int @Nullable [] segments(Kind kind, int count) {
		return switch (kind) {
			case LINES -> range(count / 2 * 2);
			case LINE_STRIP -> {
				int lines = Math.max(0, count - 1);
				int[] out = new int[lines * 2];
				for (int i = 0; i < lines; i++) {
					out[i * 2] = i;
					out[i * 2 + 1] = i + 1;
				}
				yield out;
			}
			default -> null;
		};
	}

	/**
	 * A line from a to b as a flat ribbon facing the eye, the way a line looks on screen: as wide at
	 * each end as {@code widthPerBlock} times that end's distance from the eye, so it keeps its width
	 * in pixels near and far. Its corners {a1, a2, b2, b1} as 12 floats, or null when the eye looks
	 * right along it (or it has no length).
	 */
	public static float @Nullable [] ribbon(float[] a, float[] b, float[] eye, float widthPerBlock) {
		float dx = b[0] - a[0], dy = b[1] - a[1], dz = b[2] - a[2];
		float[] sideA = side(dx, dy, dz, a, eye, widthPerBlock);
		float[] sideB = side(dx, dy, dz, b, eye, widthPerBlock);
		if (sideA == null || sideB == null) {
			return null;
		}
		return new float[] {
			a[0] - sideA[0], a[1] - sideA[1], a[2] - sideA[2],
			a[0] + sideA[0], a[1] + sideA[1], a[2] + sideA[2],
			b[0] + sideB[0], b[1] + sideB[1], b[2] + sideB[2],
			b[0] - sideB[0], b[1] - sideB[1], b[2] - sideB[2],
		};
	}

	// half the ribbon's width at p, across the line (dx, dy, dz) and the view
	private static float @Nullable [] side(float dx, float dy, float dz, float[] p, float[] eye, float widthPerBlock) {
		float vx = p[0] - eye[0], vy = p[1] - eye[1], vz = p[2] - eye[2];
		float cx = dy * vz - dz * vy, cy = dz * vx - dx * vz, cz = dx * vy - dy * vx;
		float length = (float) Math.sqrt(cx * cx + cy * cy + cz * cz);
		float distance = (float) Math.sqrt(vx * vx + vy * vy + vz * vz);
		if (length < 1.0E-9F || distance < 1.0E-6F) {
			return null;
		}
		float half = widthPerBlock * distance * 0.5F / length;
		return new float[] { cx * half, cy * half, cz * half };
	}

	private static int[] range(int count) {
		int[] out = new int[count];
		for (int i = 0; i < count; i++) {
			out[i] = i;
		}
		return out;
	}
}

package dev.halfcraft.world;

import java.util.List;

/**
 * Minecraft's explosions against Half-Life's collision triangles (pure math, like {@link HostRay}):
 * walls hide bodies behind them from a blast, and stop its block-breaking rays.
 */
public final class BlastRays {
	private BlastRays() {
	}

	/**
	 * How far above its centre an explosion looks from. A creeper's centre is its feet, right on the
	 * floor: rays from there would all start in the floor's plane, and the floor would hide everything.
	 */
	public static final double LIFT = 0.05;
	/** Triangles this close to the body's end of a ray don't hide it: its feet stand on them. */
	public static final double BODY_MARGIN = 0.05;

	/** Whether Half-Life's geometry hides the point (bx, by, bz) of a body from a blast at (cx, cy, cz). */
	public static boolean hidden(List<HostTri> tris, double cx, double cy, double cz, double bx, double by, double bz) {
		double oy = cy + LIFT;
		double dx = bx - cx, dy = by - oy, dz = bz - cz;
		double length = Math.sqrt(dx * dx + dy * dy + dz * dz);
		if (length <= BODY_MARGIN) {
			return false;
		}
		double limit = 1.0 - BODY_MARGIN / length;
		for (HostTri tri : tris) {
			if (tri.stairHelper) {
				continue;
			}
			double t = HostRay.intersect(tri, cx, oy, cz, dx, dy, dz);
			if (t >= 0.0 && t < limit) {
				return true;
			}
		}
		return false;
	}

	/**
	 * How far a blast ray from (cx, cy, cz) along the unit direction (dx, dy, dz) gets before Half-Life's
	 * geometry stops it; {@code max} when nothing does.
	 */
	public static double reach(List<HostTri> tris, double cx, double cy, double cz, double dx, double dy, double dz, double max) {
		double oy = cy + LIFT;
		double best = max;
		for (HostTri tri : tris) {
			if (tri.stairHelper) {
				continue;
			}
			double t = HostRay.intersect(tri, cx, oy, cz, dx * max, dy * max, dz * max);
			if (t >= 0.0 && t <= 1.0) {
				best = Math.min(best, t * max);
			}
		}
		return best;
	}
}

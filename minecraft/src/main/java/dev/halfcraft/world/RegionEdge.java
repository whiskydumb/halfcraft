package dev.halfcraft.world;

import org.jspecify.annotations.Nullable;

/**
 * Where a segment leaves the space Half-Life has described: it walks the collision regions (cubes of
 * {@code size} blocks) the segment passes through, in order, and stops at the first one Half-Life
 * hasn't sent. Pure logic; HostClip uses it to stop projectiles at the edge of the streamed volume.
 */
public final class RegionEdge {
	private RegionEdge() {
	}

	/** Whether Half-Life has sent region (rx, ry, rz). */
	@FunctionalInterface
	public interface Known {
		boolean region(int rx, int ry, int rz);
	}

	/**
	 * Where a segment crosses into unknown space: at {@code t} along it (0..1), through the face of an
	 * unknown region across {@code axis} (0 x, 1 y, 2 z), going in direction {@code step} (+1 or -1).
	 */
	public record Crossing(double t, int axis, int step) {
	}

	/**
	 * {@code known} plus the open sky over it: a region with a known one at most {@code sky} regions
	 * below it in its column. Half-Life streams only a few regions above the player, so a high shot
	 * would otherwise stop at the top of the volume; over known ground it flies on and comes back down
	 * into known space instead.
	 */
	public static Known withSky(Known known, int sky) {
		return (rx, ry, rz) -> {
			for (int y = ry; y >= ry - sky; y--) {
				if (known.region(rx, y, rz)) {
					return true;
				}
			}
			return false;
		};
	}

	/**
	 * The first crossing from a known region into an unknown one on the segment from (fx, fy, fz) to
	 * (tx, ty, tz), or null when it stays in known regions, or starts in an unknown one (nothing to
	 * stop it against there).
	 */
	public static @Nullable Crossing first(double fx, double fy, double fz, double tx, double ty, double tz, int size, Known known) {
		double[] from = { fx, fy, fz };
		double[] delta = { tx - fx, ty - fy, tz - fz };
		int[] cell = new int[3];
		int[] step = new int[3];
		double[] next = new double[3]; // t at the next region face along each axis
		double[] across = new double[3]; // t from one face to the next
		for (int k = 0; k < 3; k++) {
			cell[k] = Math.floorDiv((int) Math.floor(from[k]), size);
			if (delta[k] > 0.0) {
				step[k] = 1;
				next[k] = ((cell[k] + 1) * (double) size - from[k]) / delta[k];
				across[k] = size / delta[k];
			} else if (delta[k] < 0.0) {
				step[k] = -1;
				next[k] = (cell[k] * (double) size - from[k]) / delta[k];
				across[k] = -size / delta[k];
			} else {
				next[k] = Double.POSITIVE_INFINITY;
				across[k] = Double.POSITIVE_INFINITY;
			}
		}
		if (!known.region(cell[0], cell[1], cell[2])) {
			return null;
		}
		while (true) {
			int axis = next[0] <= next[1] && next[0] <= next[2] ? 0 : next[1] <= next[2] ? 1 : 2;
			double t = next[axis];
			if (t > 1.0) {
				return null;
			}
			cell[axis] += step[axis];
			next[axis] += across[axis];
			if (!known.region(cell[0], cell[1], cell[2])) {
				return new Crossing(t, axis, step[axis]);
			}
		}
	}
}

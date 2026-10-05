package dev.halfcraft.world;

/**
 * Where something lands on Half-Life's ground, from its collision voxels (see {@link NavGrid}):
 * Minecraft's random teleports (chorus fruit, endermen) look for a block to stand on under their
 * target, and Half-Life's floors are air to them. Pure logic over {@link NavGrid.Cells}.
 */
public final class HostGround {
	private HostGround() {
	}

	/**
	 * The height something teleported to (x, y, z) lands at: the first Half-Life floor at or under that
	 * point (one in its cell, or the top of a solid or a railing right under an empty cell), down to
	 * {@code maxDrop} cells and as far as Half-Life has described. A floor in the target's own cell may
	 * sit a little above it (up to 3/8 block): it lands on top of it. NaN when the point is inside
	 * Half-Life's geometry or nothing is under it.
	 */
	public static double landing(NavGrid.Cells cells, double x, double y, double z, int maxDrop) {
		int cx = (int) Math.floor(x), cy = (int) Math.floor(y), cz = (int) Math.floor(z);
		int here = NavGrid.kind(cells.nav(cx, cy, cz));
		if (here == NavGrid.SOLID || here == NavGrid.FENCE) {
			return Double.NaN;
		}
		for (int cell = cy; cell >= cy - maxDrop && cells.known(cx, cell, cz); cell--) {
			double floor = NavGrid.floorLevel(cells, cx, cell, cz);
			if (!Double.isNaN(floor)) {
				return floor;
			}
		}
		return Double.NaN;
	}
}

package dev.halfcraft.world;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import org.jspecify.annotations.Nullable;

/**
 * How Minecraft's pathfinding sees Half-Life's world: a class for each cell (block) from its 8x8x8
 * collision voxels, and box tests against those voxels for the walls between cells. Pure logic over
 * {@link Cells}; dev.halfcraft.mobs.HostNav hands it Half-Life's voxels and wires it into vanilla
 * pathfinding.
 *
 * <p>A cell is classed by its middle column only (sub-voxels 2-5 in x and z), so a wall along the
 * edge of a cell doesn't close the whole cell. Walls between the middles of two cells are found by
 * {@link #canCross}, and {@link #spot} finds where a mob fits inside a cell when an edge wall leaves
 * no room in its middle; mobs are steered to that spot.
 */
public final class NavGrid {
	/** Nothing in the middle of the cell: a mob walks through it. */
	public static final int EMPTY = 0;
	/** Ground no higher than 3/8 of the cell and clear above: a mob stands in this cell, on top of it. */
	public static final int FLOOR = 1;
	/** Geometry above 3/8 of the cell: a mob can't be in it, and stands in the cell above it. */
	public static final int SOLID = 2;
	/** Thin geometry above the floor (a railing, a pipe, a post): like a Minecraft fence, mobs don't jump it. */
	public static final int FENCE = 3;

	/** Bits a cell's class takes together with its top layer (see {@link #classify}). */
	public static final int BITS = 5;

	// the middle 4x4 sub-voxels of a layer: bit z * 8 + x for x and z in 2..5
	static final long MIDDLE = 0x00003C3C3C3C0000L;
	// GroundPathNavigation puts a standing mob's node at floor(feet + 0.5): ground up to layer 2 (3/8)
	// keeps it in the ground's own cell, from layer 3 (1/2) up it moves to the cell above
	static final int FLOOR_TOP_LAYER = 2;
	// thin geometry on a floor in the same cell is a fence once it reaches this layer: lower, it's a
	// slope or a kerb a mob may climb (a 45-degree slope can leave one row of the middle above layer 2)
	static final int FENCE_TOP_LAYER = 5;
	// seen from above, this few middle columns (or a line one voxel thick) are thin
	static final int THIN_COLUMNS = 4;
	// a box test looks at least at this much of a mob's body below its head, even when the mob is about
	// as short as the step it climbs by itself (a cat, a rabbit, a silverfish)
	static final double MIN_BAND = 0.1;
	// boxes are shrunk by this much so faces that only touch don't count
	static final double EPS = 1.0E-3;
	// how far into its cell a mob may stand off the middle to get past a wall (3/8 block)
	static final int MAX_SHIFT_EIGHTHS = 3;
	// a moving mob's box is tested this often along its way (finer than any mob's width)
	static final double SWEEP_STEP = 0.125;
	// a roof is looked for this many cells up at most
	static final int MAX_ROOF_SCAN = 48;

	/** {@link #spot}'s answer when the middle itself fits. Shared: never change it. */
	static final double[] MIDDLE_SPOT = { 0.0, 0.0 };
	// standing spots around the middle of a cell, nearest first ({dx, dz} pairs, the middle left out)
	private static final double[][] SPOTS;

	static {
		List<double[]> spots = new ArrayList<>();
		for (int dx = -MAX_SHIFT_EIGHTHS; dx <= MAX_SHIFT_EIGHTHS; dx++) {
			for (int dz = -MAX_SHIFT_EIGHTHS; dz <= MAX_SHIFT_EIGHTHS; dz++) {
				if (dx != 0 || dz != 0) {
					spots.add(new double[] { dx / 8.0, dz / 8.0 });
				}
			}
		}
		spots.sort(Comparator.comparingDouble(s -> s[0] * s[0] + s[1] * s[1]));
		SPOTS = spots.toArray(double[][]::new);
	}

	private NavGrid() {
	}

	/** Half-Life's voxels, cell by cell. */
	public interface Cells {
		/** The cell's 8 occupancy layers (bit z * 8 + x of layer y), or null when it holds no geometry. */
		long @Nullable [] layers(int x, int y, int z);

		/** Half-Life has described the region this cell is in (even if it was empty). */
		boolean known(int x, int y, int z);

		/** The cell's {@link #classify} result. */
		default int nav(int x, int y, int z) {
			long[] layers = this.layers(x, y, z);
			return layers == null ? EMPTY : classify(layers);
		}
	}

	/**
	 * A cell's class and the highest layer (0-7) its middle column reaches, packed into {@link #BITS}
	 * bits: read them back with {@link #kind} and {@link #top}.
	 */
	public static int classify(long[] layers) {
		int top = -1;
		long upper = 0;
		long all = 0;
		for (int y = 0; y < 8; y++) {
			long middle = layers[y] & MIDDLE;
			if (middle != 0) {
				top = y;
				all |= middle;
				if (y > FLOOR_TOP_LAYER) {
					upper |= middle;
				}
			}
		}
		if (top < 0) {
			return EMPTY;
		}
		int kind;
		if (upper == 0) {
			kind = FLOOR;
		} else if (thin(upper) && (thin(all) || top >= FENCE_TOP_LAYER)) {
			kind = FENCE;
		} else {
			kind = SOLID;
		}
		return top << 2 | kind;
	}

	/** EMPTY, FLOOR, SOLID or FENCE. */
	public static int kind(int nav) {
		return nav & 3;
	}

	/** The highest layer (0-7) the middle column reaches. */
	public static int top(int nav) {
		return nav >>> 2 & 7;
	}

	/** How high (1/8 to 1) the middle column reaches in its cell. */
	public static double topHeight(int nav) {
		return (top(nav) + 1) / 8.0;
	}

	/** Seen from above, the geometry covers only a few middle columns or a line of them. */
	private static boolean thin(long footprint) {
		if (Long.bitCount(footprint) <= THIN_COLUMNS) {
			return true;
		}
		int columns = 0;
		int rows = 0;
		for (int z = 0; z < 8; z++) {
			int row = (int) (footprint >>> (z * 8)) & 0xFF;
			columns |= row;
			rows += row != 0 ? 1 : 0;
		}
		return Integer.bitCount(columns) <= 1 || rows <= 1;
	}

	/** A mob may end a walk in this cell: on a Half-Life floor in it, or on a solid (not a fence) right under it. */
	public static boolean standable(Cells cells, int x, int y, int z) {
		int here = kind(cells.nav(x, y, z));
		return here == FLOOR || here == EMPTY && kind(cells.nav(x, y - 1, z)) == SOLID;
	}

	/**
	 * The height a mob in cell (x, y, z) stands at on Half-Life ground: on a floor in the cell, or on
	 * top of a solid or fence below it. NaN when the cell itself is solid or Half-Life holds nothing
	 * there to stand on.
	 */
	public static double floorLevel(Cells cells, int x, int y, int z) {
		int here = cells.nav(x, y, z);
		if (kind(here) == FLOOR) {
			return y + topHeight(here);
		}
		if (kind(here) != EMPTY) {
			return Double.NaN;
		}
		int below = cells.nav(x, y - 1, z);
		int kind = kind(below);
		return kind == SOLID || kind == FENCE ? y - 1 + topHeight(below) : Double.NaN;
	}

	/**
	 * The cell something at (x, y, z) stands in on Half-Life ground: the cell above a solid or fence
	 * it is in, else the first floor (or the cell above the first solid) down to {@code maxDrop} cells
	 * below, as far as Half-Life has described. {@link Integer#MIN_VALUE} when there is none.
	 */
	public static int standingY(Cells cells, int x, int y, int z, int maxDrop) {
		int here = kind(cells.nav(x, y, z));
		if (here == SOLID || here == FENCE) {
			return y + 1;
		}
		for (int cy = y; cy >= y - maxDrop && cells.known(x, cy, z); cy--) {
			int kind = kind(cells.nav(x, cy, z));
			if (kind == FLOOR) {
				return cy;
			}
			if (kind == SOLID || kind == FENCE) {
				return cy + 1;
			}
		}
		return Integer.MIN_VALUE;
	}

	/**
	 * Half-Life geometry shades this cell from the sky: something in the middle of it or of a cell above
	 * it, as far up as Half-Life has described.
	 */
	public static boolean roofed(Cells cells, int x, int y, int z) {
		for (int cy = y; cy < y + MAX_ROOF_SCAN && cells.known(x, cy, z); cy++) {
			if (kind(cells.nav(x, cy, z)) != EMPTY) {
				return true;
			}
		}
		return false;
	}

	/** Any Half-Life voxel overlaps the inside of the box. */
	public static boolean boxHits(Cells cells, double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
		if (!(minX < maxX && minY < maxY && minZ < maxZ)) {
			return false;
		}
		int x0 = (int) Math.floor(minX), x1 = (int) Math.ceil(maxX) - 1;
		int y0 = (int) Math.floor(minY), y1 = (int) Math.ceil(maxY) - 1;
		int z0 = (int) Math.floor(minZ), z1 = (int) Math.ceil(maxZ) - 1;
		for (int x = x0; x <= x1; x++) {
			for (int z = z0; z <= z1; z++) {
				for (int y = y0; y <= y1; y++) {
					long[] layers = cells.layers(x, y, z);
					if (layers != null && hits(layers, minX - x, minY - y, minZ - z, maxX - x, maxY - y, maxZ - z)) {
						return true;
					}
				}
			}
		}
		return false;
	}

	/** The box (cell-relative, in blocks) overlaps an occupied voxel of these layers. */
	private static boolean hits(long[] layers, double minX, double minY, double minZ, double maxX, double maxY, double maxZ) {
		int vx0 = voxelFrom(minX), vx1 = voxelTo(maxX);
		int vy0 = voxelFrom(minY), vy1 = voxelTo(maxY);
		int vz0 = voxelFrom(minZ), vz1 = voxelTo(maxZ);
		long row = (0xFFL >>> (7 - vx1)) & (0xFFL << vx0);
		long mask = 0;
		for (int vz = vz0; vz <= vz1; vz++) {
			mask |= row << (vz * 8);
		}
		for (int vy = vy0; vy <= vy1; vy++) {
			if ((layers[vy] & mask) != 0) {
				return true;
			}
		}
		return false;
	}

	// the first and last of a cell's 8 voxels (each [i/8, (i+1)/8]) that overlap (from, to)
	private static int voxelFrom(double from) {
		return Math.clamp((long) Math.floor(from * 8.0), 0, 7);
	}

	private static int voxelTo(double to) {
		return Math.clamp((long) Math.ceil(to * 8.0) - 1, 0, 7);
	}

	/**
	 * How far above its floor a box test starts on a mob's body: above the step it climbs by itself
	 * (floors, kerbs and stair risers are no walls), but low enough to leave {@link #MIN_BAND} of it.
	 */
	static double bodyBottom(double height, double step) {
		return Math.max(EPS, Math.min(step, height - MIN_BAND));
	}

	/**
	 * Where a mob of this size fits standing at {@code floor} near (cx, cz): {@link #MIDDLE_SPOT} when
	 * right there, else the nearest shift {dx, dz} of up to 3/8 block that fits, or null when nothing
	 * near does. Only its body above {@link #bodyBottom} counts.
	 */
	public static double @Nullable [] spot(Cells cells, double cx, double floor, double cz, double width, double height, double step) {
		double lo = floor + bodyBottom(height, step);
		double hi = floor + height - EPS;
		double r = width / 2.0 - EPS;
		if (!boxHits(cells, cx - r, lo, cz - r, cx + r, hi, cz + r)) {
			return MIDDLE_SPOT;
		}
		for (double[] spot : SPOTS) {
			double x = cx + spot[0], z = cz + spot[1];
			if (!boxHits(cells, x - r, lo, z - r, x + r, hi, z + r)) {
				return spot;
			}
		}
		return null;
	}

	/**
	 * Half-Life lets a mob walk from its spot near (ax, az), standing at {@code floorA}, to its spot near
	 * the neighbouring (bx, bz), standing at {@code floorB}, without hitting a wall: its box is swept
	 * along the way between the two {@link #spot}s. Only its body above {@link #bodyBottom} over the
	 * higher floor counts, up to its head over the lower one (stairs and floors don't), but at least
	 * {@link #MIN_BAND} of it.
	 */
	public static boolean canCross(Cells cells, double ax, double az, double floorA, double bx, double bz, double floorB, double width, double height, double step) {
		double bottom = bodyBottom(height, step);
		double r = width / 2.0 - EPS;
		// open ground first: nothing around either middle or between them, over either floor, so both
		// spots are the middles and the way between them is clear
		double lowFloor = Math.min(floorA, floorB), highFloor = Math.max(floorA, floorB);
		if (!boxHits(cells, Math.min(ax, bx) - r, lowFloor + bottom, Math.min(az, bz) - r, Math.max(ax, bx) + r, highFloor + height - EPS, Math.max(az, bz) + r)) {
			return true;
		}
		double[] from = spot(cells, ax, floorA, az, width, height, step);
		double[] to = spot(cells, bx, floorB, bz, width, height, step);
		if (from == null || to == null) {
			return false;
		}
		double lo = highFloor + bottom;
		double hi = Math.max(lowFloor + height, lo + MIN_BAND) - EPS;
		double x0 = ax + from[0], z0 = az + from[1];
		double x1 = bx + to[0], z1 = bz + to[1];
		int steps = Math.max(1, (int) Math.ceil(Math.max(Math.abs(x1 - x0), Math.abs(z1 - z0)) / SWEEP_STEP));
		for (int i = 0; i <= steps; i++) {
			double t = (double) i / steps;
			double x = x0 + (x1 - x0) * t, z = z0 + (z1 - z0) * t;
			if (boxHits(cells, x - r, lo, z - r, x + r, hi, z + r)) {
				return false;
			}
		}
		return true;
	}
}

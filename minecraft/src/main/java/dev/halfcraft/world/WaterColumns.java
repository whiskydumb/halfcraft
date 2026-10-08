package dev.halfcraft.world;

import java.util.ArrayList;
import java.util.Collection;
import java.util.Comparator;
import java.util.List;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's water surface over Minecraft's block columns, as client.dll's grids describe it: the
 * player's WaterGrid and the small grids of the water probes around boats, fishing bobbers, mobs and
 * dropped items (see HostWater). Pure logic.
 */
public final class WaterColumns {
	/** Below this a grid's surface means "no water in this column" (the protocol's kNoWater). */
	static final float NO_WATER = -1.0E20F;

	private WaterColumns() {
	}

	/**
	 * A square of block columns: surface[z * size + x] is the Minecraft y of the water surface over
	 * (originX + x, originZ + z). Its search for the surface went down to {@code bottom}: under that it
	 * knows nothing. The player's grid has none (-infinity): its water reaches down through every cell.
	 */
	public record Grid(int originX, int originZ, int size, float[] surface, double bottom) {
	}

	/**
	 * A thing (entity id) that wants its water probed, how much (rank: lower first), where it is
	 * (Minecraft coords), and when it last asked (milliseconds).
	 */
	public record Want(int id, int rank, double x, double y, double z, long askedMs) {
	}

	/** The surface over column (x, z) in this grid, or NaN outside it or where it found no water. */
	public static double surfaceIn(@Nullable Grid grid, int x, int z) {
		if (grid == null) {
			return Double.NaN;
		}
		int dx = x - grid.originX(), dz = z - grid.originZ();
		if (dx < 0 || dz < 0 || dx >= grid.size() || dz >= grid.size()) {
			return Double.NaN;
		}
		float s = grid.surface()[dz * grid.size() + dx];
		return s < NO_WATER ? Double.NaN : s;
	}

	/**
	 * The surface over column (x, z) for the cell at height y: what the player's grid says, else the
	 * first probe grid that has water there and searched down into that cell. A probe reaches deeper
	 * or higher than the player's grid where its thing is far below or above the player (a bobber cast
	 * down into a canal), so "none" in one grid doesn't overrule water another one found; but under
	 * where a probe looked (the air under a raised pool) it says nothing.
	 */
	public static double surface(@Nullable Grid player, List<Grid> probes, int x, int y, int z) {
		double s = surfaceIn(player, x, z);
		for (int i = 0; Double.isNaN(s) && i < probes.size(); i++) {
			Grid probe = probes.get(i);
			if (y + 1 > probe.bottom()) {
				s = surfaceIn(probe, x, z);
			}
		}
		return s;
	}

	/**
	 * Whether a grid looked for water at the cell (x, y, z), so {@link #surface} there is an answer (water
	 * or none) rather than "nobody looked": the player's grid over its columns at any height, a probe's
	 * over its columns down to where its search stopped.
	 */
	public static boolean known(@Nullable Grid player, List<Grid> probes, int x, int y, int z) {
		if (covers(player, x, z)) {
			return true;
		}
		for (Grid probe : probes) {
			if (y + 1 > probe.bottom() && covers(probe, x, z)) {
				return true;
			}
		}
		return false;
	}

	private static boolean covers(@Nullable Grid grid, int x, int z) {
		if (grid == null) {
			return false;
		}
		int dx = x - grid.originX(), dz = z - grid.originZ();
		return dx >= 0 && dz >= 0 && dx < grid.size() && dz < grid.size();
	}

	/**
	 * Which things get a probe: the ones that asked within {@code maxAgeMs}, the lowest rank first and
	 * of a rank the nearest to (cx, cz) (the middle of the player's grid), at most {@code max}. The order
	 * holds while they stay put, so each keeps its probe's slot.
	 */
	public static List<Want> choose(Collection<Want> wants, double cx, double cz, long nowMs, long maxAgeMs, int max) {
		List<Want> fresh = new ArrayList<>();
		for (Want want : wants) {
			if (nowMs - want.askedMs() <= maxAgeMs) {
				fresh.add(want);
			}
		}
		fresh.sort(Comparator.comparingInt(Want::rank).thenComparingDouble(w -> (w.x() - cx) * (w.x() - cx) + (w.z() - cz) * (w.z() - cz)));
		return fresh.size() > max ? new ArrayList<>(fresh.subList(0, max)) : fresh;
	}
}

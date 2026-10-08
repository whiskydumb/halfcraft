package dev.halfcraft.world;

import org.jspecify.annotations.Nullable;

/**
 * Where Minecraft's water and lava may flow among Half-Life's geometry (FlowingFluidMixin). A cell's
 * floor is the highest walkable surface under its middle. Half-Life's geometry arrives as its surfaces
 * (the inside of a thick brush has no voxels, and which side of a triangle is solid isn't known), so:
 * <ul>
 * <li>a fluid moves sideways into a cell only if nothing stands between it and that cell's middle (a
 * wall crossing the far side of the cell lets it in, one on the near side or a brush the middle is
 * inside of doesn't), and an empty cell under geometry is under the ground;</li>
 * <li>it falls only where nothing lies under the middle of its cell, below its surface, so with just a
 * wall beside it in the cell it falls past.</li>
 * </ul>
 * Pure logic over a cell's floor height (0..1, HostCollision.floorTop) and its 8 voxel layers (bit
 * z * 8 + x of layer y, HostCollision.layersAt; null: no geometry there).
 */
public final class FluidCells {
	/** A falling fluid's surface in its cell (8/9). */
	public static final float FALLING_SURFACE = 8.0F / 9.0F;
	/** How far a fluid's surface must clear the ground it moves over. */
	public static final float MARGIN = 0.05F;
	/** Level or downhill: a floor this little higher than the one a fluid leaves still takes it (a voxel). */
	private static final float LEVEL = 0.13F;
	// the cell's middle voxels: x and z 3..4 of 0..7
	private static final long MIDDLE = 1L << (3 * 8 + 3) | 1L << (3 * 8 + 4) | 1L << (4 * 8 + 3) | 1L << (4 * 8 + 4);

	private FluidCells() {
	}

	/** Whether geometry fills any of the cell's middle voxels between heights {@code from} and {@code to} (0..1). */
	public static boolean middleBlocked(long @Nullable [] layers, double from, double to) {
		if (layers == null || to <= from) {
			return false;
		}
		int lo = Math.max(0, (int) Math.floor(from * 8.0));
		int hi = Math.min(7, (int) Math.ceil(to * 8.0) - 1);
		for (int y = lo; y <= hi; y++) {
			if ((layers[y] & MIDDLE) != 0) {
				return true;
			}
		}
		return false;
	}

	/**
	 * Why a fluid may not fall from its cell into the one below, or null when it may: something lies
	 * under the middle of its cell below its surface (a floor, a slope, one right at the bottom), or the
	 * cell below is ground up to where the fluid would land.
	 *
	 * @param height - the fluid's own height in its cell (0..1)
	 * @param floor - the floor under its middle, below that height
	 */
	public static @Nullable String down(float height, float floor, long @Nullable [] layers, float belowFloor, long @Nullable [] belowLayers) {
		if (floor > 0.0F || middleBlocked(layers, 0.0, Math.max(height, 1.0 / 8.0)) || middleBlocked(belowLayers, 7.0 / 8.0, 1.0)) {
			return "resting on ground";
		}
		if (belowFloor >= FALLING_SURFACE - MARGIN) {
			return "ground below";
		}
		return null;
	}

	/**
	 * Why a fluid may not flow sideways into a cell, or null when it may.
	 *
	 * @param surface - the fluid's surface there (0..1)
	 * @param floor - the floor of the cell it leaves
	 * @param targetFloor - the floor of the cell it'd flow into
	 * @param targetLayers - that cell's voxels (null: no geometry)
	 * @param aboveLayers - the voxels of the cell above that one
	 * @param reachable - nothing stands between the fluid and that cell's middle, just above its floor
	 */
	public static @Nullable String sideways(float surface, float floor, float targetFloor, long @Nullable [] targetLayers, long @Nullable [] aboveLayers,
		boolean reachable) {
		if (targetLayers == null) {
			// an empty cell under geometry: inside a brush, under the terrain or an overhang
			return aboveLayers != null ? "under ground" : null;
		}
		if (!reachable) {
			return "wall beside";
		}
		boolean levelOrDownhill = targetFloor <= floor + LEVEL;
		return !levelOrDownhill && targetFloor >= surface - MARGIN ? "ground beside" : null;
	}
}

package dev.halfcraft.world;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.HostLink;
import java.lang.foreign.MemorySegment;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.core.BlockPos;
import net.minecraft.world.phys.shapes.BitSetDiscreteVoxelShape;
import net.minecraft.world.phys.shapes.CubeVoxelShape;
import net.minecraft.world.phys.shapes.Shapes;
import net.minecraft.world.phys.shapes.VoxelShape;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's world geometry as Minecraft sees it: an 8x8x8 sub-voxel collision shape per block
 * position, streamed from Half-Life's server.dll. These are not blocks; they are merged into block
 * collision queries (see BlockCollisionsMixin) so vanilla movement code collides with them.
 */
public final class HostCollision {
	/** Half-Life regions are streamed as cubes of this many blocks. Must match the host side. */
	public static final int REGION_SIZE = 8;

	private static final ConcurrentHashMap<Long, VoxelShape> SHAPES = new ConcurrentHashMap<>();
	// Per block: sub-voxel count (bits 0-9), any in the lower half (bit 10), any in the upper half (bit 11).
	private static final ConcurrentHashMap<Long, Integer> FILL = new ConcurrentHashMap<>();
	private static final int FILL_LOWER = 1 << 10;
	private static final int FILL_UPPER = 1 << 11;
	private static final int FILL_NAV_SHIFT = 16; // how mobs path through the cell (NavGrid.classify)
	private static final int FILL_SKY = 1 << 24; // all of it is the map's sky: no roof over what's under it
	// Per block: its 8 occupancy layers, for the box tests mobs' pathfinding makes between cells (NavGrid).
	private static final ConcurrentHashMap<Long, long[]> LAYERS = new ConcurrentHashMap<>();
	private static final ConcurrentHashMap<Long, HostTri[]> TRIS = new ConcurrentHashMap<>();
	private static volatile java.util.function.Predicate<net.minecraft.world.entity.Entity> smoothCollider = e -> false;
	private static final Set<Long> KNOWN_REGIONS = ConcurrentHashMap.newKeySet();
	private static volatile int epoch = -1;
	private static long lostPlaceLogged; // when the ring's lost place was last logged (ms)
	private static Thread consumer;

	private HostCollision() {
	}

	public static @Nullable VoxelShape shapeAt(BlockPos pos) {
		return SHAPES.isEmpty() ? null : SHAPES.get(pos.asLong());
	}

	/** Entities (the local player) that collide with Half-Life's exact triangles instead of its voxels. */
	public static void setSmoothCollider(java.util.function.Predicate<net.minecraft.world.entity.Entity> predicate) {
		smoothCollider = predicate;
	}

	public static boolean usesSmoothCollider(net.minecraft.world.entity.@Nullable Entity entity) {
		return entity != null && smoothCollider.test(entity);
	}

	/** Adds every Half-Life triangle whose bounds overlap {@code box}. */
	public static void trianglesNear(net.minecraft.world.phys.AABB box, java.util.List<HostTri> out) {
		if (TRIS.isEmpty()) {
			return;
		}
		int rx0 = Math.floorDiv((int) Math.floor(box.minX), REGION_SIZE), rx1 = Math.floorDiv((int) Math.floor(box.maxX), REGION_SIZE);
		int ry0 = Math.floorDiv((int) Math.floor(box.minY), REGION_SIZE), ry1 = Math.floorDiv((int) Math.floor(box.maxY), REGION_SIZE);
		int rz0 = Math.floorDiv((int) Math.floor(box.minZ), REGION_SIZE), rz1 = Math.floorDiv((int) Math.floor(box.maxZ), REGION_SIZE);
		for (int rx = rx0; rx <= rx1; rx++) {
			for (int ry = ry0; ry <= ry1; ry++) {
				for (int rz = rz0; rz <= rz1; rz++) {
					HostTri[] tris = TRIS.get(regionKey(rx, ry, rz));
					if (tris == null) {
						continue;
					}
					for (HostTri t : tris) {
						if (t.maxX >= box.minX && t.minX <= box.maxX && t.maxY >= box.minY && t.minY <= box.maxY && t.maxZ >= box.minZ && t.minZ <= box.maxZ) {
							out.add(t);
						}
					}
				}
			}
		}
	}

	private static void near(ConcurrentHashMap<Long, HostTri[]> store, net.minecraft.world.phys.AABB box, java.util.List<HostTri> out) {
		if (store.isEmpty()) {
			return;
		}
		int rx0 = Math.floorDiv((int) Math.floor(box.minX), REGION_SIZE), rx1 = Math.floorDiv((int) Math.floor(box.maxX), REGION_SIZE);
		int ry0 = Math.floorDiv((int) Math.floor(box.minY), REGION_SIZE), ry1 = Math.floorDiv((int) Math.floor(box.maxY), REGION_SIZE);
		int rz0 = Math.floorDiv((int) Math.floor(box.minZ), REGION_SIZE), rz1 = Math.floorDiv((int) Math.floor(box.maxZ), REGION_SIZE);
		for (int rx = rx0; rx <= rx1; rx++) {
			for (int ry = ry0; ry <= ry1; ry++) {
				for (int rz = rz0; rz <= rz1; rz++) {
					HostTri[] tris = store.get(regionKey(rx, ry, rz));
					if (tris == null) {
						continue;
					}
					for (HostTri t : tris) {
						if (t.maxX >= box.minX && t.minX <= box.maxX && t.maxY >= box.minY && t.minY <= box.maxY && t.maxZ >= box.minZ && t.minZ <= box.maxZ) {
							out.add(t);
						}
					}
				}
			}
		}
	}

	/** True once Half-Life has sent the region containing this block (even if it was empty). */
	public static boolean isKnown(int x, int y, int z) {
		return KNOWN_REGIONS.contains(regionKey(Math.floorDiv(x, REGION_SIZE), Math.floorDiv(y, REGION_SIZE), Math.floorDiv(z, REGION_SIZE)));
	}

	/** True if any Half-Life geometry exists in the 3x3 column below (x, y, z), down to {@code depth} blocks. */
	public static boolean hasSolidBelow(int x, int y, int z, int depth) {
		for (int dy = 0; dy <= depth; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				for (int dz = -1; dz <= 1; dz++) {
					if (SHAPES.containsKey(BlockPos.asLong(x + dx, y - dy, z + dz))) {
						return true;
					}
				}
			}
		}
		return false;
	}

	/** Fraction (0..1) of this block's volume that is Half-Life geometry. */
	public static float solidFraction(BlockPos pos) {
		Integer fill = FILL.isEmpty() ? null : FILL.get(pos.asLong());
		return fill == null ? 0.0F : (fill & 0x3FF) / 512.0F;
	}

	/** How mobs path through this cell: a {@link NavGrid#classify} result (NavGrid.EMPTY without geometry). */
	public static int navAt(int x, int y, int z) {
		Integer fill = FILL.isEmpty() ? null : FILL.get(BlockPos.asLong(x, y, z));
		return fill == null ? NavGrid.EMPTY : fill >>> FILL_NAV_SHIFT & ((1 << NavGrid.BITS) - 1);
	}

	/** All of this cell's geometry is the map's sky (its skybox ceiling): what's under it is out in the open. */
	public static boolean isSkyAt(int x, int y, int z) {
		Integer fill = FILL.isEmpty() ? null : FILL.get(BlockPos.asLong(x, y, z));
		return fill != null && (fill & FILL_SKY) != 0;
	}

	/** This cell's 8 occupancy layers (bit z * 8 + x of layer y), or null without geometry. Never change them. */
	public static long @Nullable [] layersAt(int x, int y, int z) {
		return LAYERS.isEmpty() ? null : LAYERS.get(BlockPos.asLong(x, y, z));
	}

	/** True if any Half-Life geometry is in this cell. */
	public static boolean hasGeometry(BlockPos pos) {
		return !FILL.isEmpty() && FILL.containsKey(pos.asLong());
	}

	/** A fluid keeps at least this much of its cell above the Half-Life floor it rests on. */
	private static final float MAX_FLUID_FLOOR = 0.95F;

	/**
	 * Height (0..1) of the Half-Life floor under the middle of this cell, below {@code surface} (a
	 * fluid's own height there): the highest walkable triangle, or 0. Walls, stair risers and solid
	 * brushes reaching the top of the cell don't count (FluidCells sees those in the voxels).
	 */
	public static float floorTop(BlockPos pos, float surface) {
		if (!hasGeometry(pos)) {
			return 0.0F;
		}
		return floorUnder(pos.getX() + 0.5, pos.getY(), pos.getZ() + 0.5, Math.max(0.05, surface));
	}

	/**
	 * Height (0..1) of the Half-Life floor under (x, z) in the row of cells at {@code cellY}: the highest
	 * walkable triangle there, or 0. A fluid's corners rest on it, so neighbouring cells, which share
	 * their corners, draw one surface over sloping ground.
	 */
	public static float floorAt(double x, int cellY, double z) {
		return floorUnder(x, cellY, z, 1.0);
	}

	private static float floorUnder(double x, int y0, double z, double above) {
		double top = y0 + above;
		java.util.List<HostTri> tris = new java.util.ArrayList<>();
		trianglesNear(new net.minecraft.world.phys.AABB(x - 0.01, y0, z - 0.01, x + 0.01, top, z + 0.01), tris);
		tris.removeIf(t -> !t.walkable);
		HostRay.Hit hit = HostRay.cast(tris, x, top, z, x, y0, z);
		return hit == null ? 0.0F : (float) Math.clamp(hit.y() - y0, 0.0, MAX_FLUID_FLOOR);
	}

	/** Whether any Half-Life geometry crosses the segment from (fx, fy, fz) to (tx, ty, tz). */
	public static boolean blocked(double fx, double fy, double fz, double tx, double ty, double tz) {
		java.util.List<HostTri> tris = new java.util.ArrayList<>();
		trianglesNear(new net.minecraft.world.phys.AABB(fx, fy, fz, tx, ty, tz).inflate(0.01), tris);
		return !tris.isEmpty() && HostRay.cast(tris, fx, fy, fz, tx, ty, tz) != null;
	}

	/** True if Half-Life ground holds up whatever is in this cell (terrain in its lower half or the top of the cell below). */
	public static boolean supportsFromBelow(BlockPos pos) {
		if (FILL.isEmpty()) {
			return false;
		}
		Integer here = FILL.get(pos.asLong());
		if (here != null && (here & FILL_LOWER) != 0) {
			return true;
		}
		Integer below = FILL.get(BlockPos.asLong(pos.getX(), pos.getY() - 1, pos.getZ()));
		return below != null && (below & FILL_UPPER) != 0;
	}

	public static int blockCount() {
		return SHAPES.size();
	}

	public static int regionCount() {
		return KNOWN_REGIONS.size();
	}

	/** Half-Life is describing its world around the player (false in a plain Minecraft world). */
	public static boolean active() {
		return !KNOWN_REGIONS.isEmpty();
	}

	private static long regionKey(int rx, int ry, int rz) {
		return BlockPos.asLong(rx, ry, rz);
	}

	public static synchronized void startConsumer() {
		if (consumer != null) {
			return;
		}
		consumer = new Thread(HostCollision::consumeLoop, "HalfCraft collision");
		consumer.setDaemon(true);
		consumer.start();
	}

	private static void consumeLoop() {
		while (true) {
			try {
				if (!drainOnce()) {
					Thread.sleep(2);
				}
			} catch (InterruptedException e) {
				return;
			} catch (Throwable t) {
				HalfCraft.LOG.error("HalfCraft: collision consumer error", t);
				try {
					Thread.sleep(500);
				} catch (InterruptedException e) {
					return;
				}
			}
		}
	}

	/** Processes all pending collision messages. Returns true if anything was consumed. */
	private static boolean drainOnce() {
		MemorySegment s = HostLink.segment();
		if (s == null) {
			return false;
		}
		long head = HostLink.collisionHead();
		long tail = HostLink.collisionTail();
		if (tail > head || head - tail > CR_DATA_BYTES) {
			// the ring's indices disagree: what's between isn't Half-Life's messages
			lostPlace(head, tail);
			return false;
		}
		if (tail == head) {
			return false;
		}
		long data = OFF_COLLISION_RING + CR_DATA;
		while (tail < head) {
			long pos = tail % CR_DATA_BYTES;
			int type = s.get(JAVA_INT, data + pos + COL_MSG_HEADER_TYPE);
			int payloadBytes = s.get(JAVA_INT, data + pos + COL_MSG_HEADER_PAYLOAD_BYTES);
			if (type == COL_PAD) {
				tail += CR_DATA_BYTES - pos;
				continue;
			}
			if (CR_DATA_BYTES - pos < COL_MSG_HEADER_BYTES || payloadBytes < 0 || payloadBytes > CR_DATA_BYTES - pos - COL_MSG_HEADER_BYTES) {
				// not a message Half-Life wrote
				lostPlace(head, tail);
				return true;
			}
			long payload = data + pos + COL_MSG_HEADER_BYTES;
			switch (type) {
				case COL_CLEAR -> clear(s.get(JAVA_INT, payload));
				case COL_REGION -> readRegion(s, payload);
				case COL_TRIS -> readTris(s, payload);
				default -> HalfCraft.LOG.warn("HalfCraft: unknown collision message {}", type);
			}
			tail += align8(COL_MSG_HEADER_BYTES + payloadBytes);
		}
		HostLink.setCollisionTail(tail);
		return true;
	}

	private static long align8(long v) {
		return (v + 7) & ~7L;
	}

	/**
	 * The collision ring lost its place (never in a normal run): skip to what Half-Life wrote last and
	 * have it stream its collision afresh, which clears what's here first.
	 */
	private static void lostPlace(long head, long tail) {
		long now = System.currentTimeMillis();
		if (now - lostPlaceLogged >= 60_000L) {
			lostPlaceLogged = now;
			HalfCraft.LOG.warn("HalfCraft: collision ring out of step (written up to {}, read up to {}): resyncing", head, tail);
		}
		HostLink.setCollisionTail(head);
		HostLink.pushEvent(EV_COLLISION_LOST, 0, 0.0F, 0.0F, 0.0F, 0.0F, 0);
	}

	/** A freshly started client joins whatever collision epoch Half-Life is already on. */
	private static void adoptEpochIfFresh(int msgEpoch) {
		if (epoch == -1) {
			epoch = msgEpoch;
			HalfCraft.LOG.info("HalfCraft: joined collision epoch {} already in progress", msgEpoch);
		}
	}

	private static void clear(int newEpoch) {
		// forgotten before the shapes go: whatever holds still until its region is known (the player,
		// mobs) never sees a known region without its ground
		KNOWN_REGIONS.clear();
		SHAPES.clear();
		FILL.clear();
		LAYERS.clear();
		TRIS.clear();
		epoch = newEpoch;
		HalfCraft.LOG.info("HalfCraft: collision cleared (epoch {})", newEpoch);
	}

	private static void readRegion(MemorySegment s, long p) {
		int minX = s.get(JAVA_INT, p + COL_REGION_MIN_X);
		int minY = s.get(JAVA_INT, p + COL_REGION_MIN_Y);
		int minZ = s.get(JAVA_INT, p + COL_REGION_MIN_Z);
		int maxX = s.get(JAVA_INT, p + COL_REGION_MAX_X);
		int maxY = s.get(JAVA_INT, p + COL_REGION_MAX_Y);
		int maxZ = s.get(JAVA_INT, p + COL_REGION_MAX_Z);
		int msgEpoch = s.get(JAVA_INT, p + COL_REGION_EPOCH);
		int count = s.get(JAVA_INT, p + COL_REGION_COUNT);
		adoptEpochIfFresh(msgEpoch);
		if (msgEpoch != epoch) {
			return; // stale region from before a world change
		}

		// Build the new shapes first so readers never see a half-empty region.
		java.util.HashMap<Long, VoxelShape> fresh = new java.util.HashMap<>(count * 2);
		java.util.HashMap<Long, Integer> freshFill = new java.util.HashMap<>(count * 2);
		java.util.HashMap<Long, long[]> freshLayers = new java.util.HashMap<>(count * 2);
		long e = p + COL_REGION_HEADER_BYTES;
		for (int i = 0; i < count; i++, e += COL_BLOCK_BYTES) {
			int x = s.get(JAVA_INT, e + COL_BLOCK_X);
			int y = s.get(JAVA_INT, e + COL_BLOCK_Y);
			int z = s.get(JAVA_INT, e + COL_BLOCK_Z);
			boolean sky = (s.get(JAVA_INT, e + COL_BLOCK_FLAGS) & COL_BLOCK_SKY) != 0;
			long[] layers = readLayers(s, e + COL_BLOCK_BITS);
			VoxelShape shape = buildShape(layers);
			if (shape != null) {
				long key = BlockPos.asLong(x, y, z);
				fresh.put(key, shape);
				freshFill.put(key, fillInfo(layers) | (sky ? FILL_SKY : 0));
				freshLayers.put(key, layers);
			}
		}

		for (int x = minX; x <= maxX; x++) {
			for (int y = minY; y <= maxY; y++) {
				for (int z = minZ; z <= maxZ; z++) {
					long key = BlockPos.asLong(x, y, z);
					VoxelShape shape = fresh.get(key);
					if (shape != null) {
						SHAPES.put(key, shape);
						FILL.put(key, freshFill.get(key));
						LAYERS.put(key, freshLayers.get(key));
					} else {
						SHAPES.remove(key);
						FILL.remove(key);
						LAYERS.remove(key);
					}
				}
			}
		}

		for (int rx = Math.floorDiv(minX, REGION_SIZE); rx <= Math.floorDiv(maxX, REGION_SIZE); rx++) {
			for (int ry = Math.floorDiv(minY, REGION_SIZE); ry <= Math.floorDiv(maxY, REGION_SIZE); ry++) {
				for (int rz = Math.floorDiv(minZ, REGION_SIZE); rz <= Math.floorDiv(maxZ, REGION_SIZE); rz++) {
					KNOWN_REGIONS.add(regionKey(rx, ry, rz));
				}
			}
		}
	}

	private static void readTris(MemorySegment s, long p) {
		int minX = s.get(JAVA_INT, p + COL_REGION_MIN_X);
		int minY = s.get(JAVA_INT, p + COL_REGION_MIN_Y);
		int minZ = s.get(JAVA_INT, p + COL_REGION_MIN_Z);
		int msgEpoch = s.get(JAVA_INT, p + COL_REGION_EPOCH);
		int count = s.get(JAVA_INT, p + COL_REGION_COUNT);
		adoptEpochIfFresh(msgEpoch);
		if (msgEpoch != epoch) {
			return;
		}
		HostTri[] tris = new HostTri[count];
		float[] v = new float[9];
		int kept = 0;
		long e = p + COL_REGION_HEADER_BYTES;
		for (int i = 0; i < count; i++, e += COL_TRI_BYTES) {
			for (int k = 0; k < 9; k++) {
				v[k] = s.get(JAVA_FLOAT, e + COL_TRI_V + k * 4L);
			}
			HostTri t = new HostTri(v, 0, s.get(JAVA_INT, e + COL_TRI_FLAGS));
			if (!t.degenerate()) {
				tris[kept++] = t;
			}
		}
		long region = regionKey(Math.floorDiv(minX, REGION_SIZE), Math.floorDiv(minY, REGION_SIZE), Math.floorDiv(minZ, REGION_SIZE));
		TRIS.put(region, java.util.Arrays.copyOf(tris, kept));
	}

	public static int triangleCount() {
		int n = 0;
		for (HostTri[] t : TRIS.values()) {
			n += t.length;
		}
		return n;
	}

	private static long[] readLayers(MemorySegment s, long bitsOff) {
		long[] layers = new long[8];
		for (int y = 0; y < 8; y++) {
			layers[y] = s.get(JAVA_LONG, bitsOff + y * 8L);
		}
		return layers;
	}

	private static int fillInfo(long[] layers) {
		int count = 0;
		int info = 0;
		for (int y = 0; y < 8; y++) {
			long layer = layers[y];
			count += Long.bitCount(layer);
			if (layer != 0) {
				info |= y < 4 ? FILL_LOWER : FILL_UPPER;
			}
		}
		return info | count | NavGrid.classify(layers) << FILL_NAV_SHIFT;
	}

	private static @Nullable VoxelShape buildShape(long[] layers) {
		boolean any = false;
		boolean full = true;
		for (int y = 0; y < 8; y++) {
			any |= layers[y] != 0;
			full &= layers[y] == -1L;
		}
		if (!any) {
			return null;
		}
		if (full) {
			return Shapes.block();
		}
		BitSetDiscreteVoxelShape discrete = new BitSetDiscreteVoxelShape(8, 8, 8);
		for (int y = 0; y < 8; y++) {
			long layer = layers[y];
			while (layer != 0) {
				int bit = Long.numberOfTrailingZeros(layer);
				layer &= layer - 1;
				discrete.fill(bit & 7, y, bit >>> 3);
			}
		}
		return new CubeVoxelShape(discrete);
	}
}

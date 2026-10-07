package dev.halfcraft.world;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.HostLink;
import dev.halfcraft.link.Proto;
import dev.halfcraft.link.WaterProbes;
import dev.halfcraft.mobs.HostNav;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.core.BlockPos;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.vehicle.boat.AbstractBoat;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.Blocks;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.level.material.Fluids;
import net.minecraft.world.phys.Vec3;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's water (canals, pools) as Minecraft water: Half-Life sends the water surface over the block
 * columns around the player (see WaterGrid in the protocol), and wherever Minecraft has air below
 * that surface, entities treat it as water, so the player swims, floats, sinks slowly and drowns
 * there as in Minecraft water. Only entity physics sees it; no blocks change.
 *
 * <p>Boats and fishing bobbers ask for water of their own ({@link #track}): client.dll probes a small
 * grid around each of the nearest few (WaterProbes in the protocol), which reaches them far from the
 * player or far below.
 */
public final class HostWater {
	// a thing that stopped asking for this long gets no probe any more (gone, or out of the world)
	private static final long WANT_MS = 1000;
	// how finely a line is followed looking for the water surface (blocks)
	private static final double RAY_STEP = 1.0 / 16.0;

	private static volatile WaterColumns.@Nullable Grid grid;
	private static volatile List<WaterColumns.Grid> probes = List.of();
	// entity id -> where it wants Half-Life's water probed (client and server ticks alike)
	private static final ConcurrentHashMap<Integer, WaterColumns.Want> WANTS = new ConcurrentHashMap<>();
	private static int probesAsked;
	// the entities a probe is chosen for (render thread writes, server thread reads)
	private static volatile Set<Integer> probed = Set.of();
	// server thread: boats waiting for their water this tick, and when that was last logged
	private static final long WAIT_LOG_MS = 5000;
	private static int waiting;
	private static long waitLoggedMs;

	private HostWater() {
	}

	/** Once a frame on the client: pick up Half-Life's latest grids and say where probes are wanted. */
	public static void refresh() {
		HostLink.WaterGrid read = HostLink.readWaterGrid();
		if (read != null) {
			grid = new WaterColumns.Grid(read.originX, read.originZ, read.size, read.surface, Double.NEGATIVE_INFINITY);
		}
		WaterColumns.Grid g = grid;
		if (g == null) {
			return;
		}
		long now = System.currentTimeMillis();
		WANTS.values().removeIf(want -> now - want.askedMs() > WANT_MS);
		List<WaterColumns.Want> chosen = WaterColumns.choose(WANTS.values(), g.originX() + g.size() / 2.0, g.originZ() + g.size() / 2.0, now, WANT_MS,
			Proto.MAX_WATER_PROBES);
		List<double[]> asks = new ArrayList<>(chosen.size());
		Set<Integer> ids = new HashSet<>();
		for (WaterColumns.Want want : chosen) {
			asks.add(new double[] { want.x(), want.y(), want.z() });
			ids.add(want.id());
		}
		probed = ids;
		WaterProbes.writeRequests(asks);
		if (asks.size() != probesAsked) {
			probesAsked = asks.size();
			HalfCraft.LOG.info("HalfCraft: asking Half-Life for its water around {} boat(s) or bobber(s)", probesAsked);
		}
		List<WaterColumns.Grid> answers = WaterProbes.read();
		if (answers != null) {
			probes = answers;
		}
	}

	public static void clear() {
		grid = null;
		probes = List.of();
		probed = Set.of();
		WANTS.clear();
		probesAsked = 0;
	}

	/**
	 * A boat or a fishing bobber in the mirror world asks, every tick it lives, for Half-Life's water
	 * where it is: the player's grid alone doesn't reach a boat left behind or a bobber cast far out or
	 * down. It always asks, even where the player's grid covers it, so the probes don't come and go
	 * (and log) as it drifts across that grid's edge.
	 */
	public static void track(Entity entity) {
		if (grid == null || !HostNav.inMirror(entity.level())) {
			return;
		}
		WANTS.put(entity.getId(), new WaterColumns.Want(entity.getId(), entity.getX(), entity.getY(), entity.getZ(), System.currentTimeMillis()));
	}

	public static boolean active() {
		return grid != null;
	}

	/**
	 * A boat where no grid has looked for Half-Life's water yet waits for the probe it's getting, as
	 * things wait for Half-Life's ground after a load (EntityHoldMixin): falling in before it, a boat
	 * dropped onto far water would pass the surface it doesn't know of, land on the bottom and stay
	 * under, since a boat only comes up when it falls into water. Only the boats a probe is chosen for
	 * wait (a frame or two): the rest have none coming. Server side, where the boat's physics run
	 * unless the player steers it.
	 */
	public static boolean holdUntilWaterKnown(Entity entity) {
		WaterColumns.Grid g = grid;
		if (g == null || !(entity instanceof AbstractBoat) || entity.level().isClientSide() || !probed.contains(entity.getId())
			|| !HostNav.inMirror(entity.level())) {
			return false;
		}
		int x = Mth.floor(entity.getX()), y = Mth.floor(entity.getY()), z = Mth.floor(entity.getZ());
		List<WaterColumns.Grid> p = probes;
		if (WaterColumns.known(g, p, x, y, z) && WaterColumns.known(g, p, x, y - 1, z)) {
			return false;
		}
		entity.setDeltaMovement(Vec3.ZERO);
		waiting++;
		return true;
	}

	/** End of a server tick: says now and then that boats waited for their water (each waits a frame or two). */
	public static void endTick() {
		int count = waiting;
		waiting = 0;
		long now = System.currentTimeMillis();
		if (count > 0 && now - waitLoggedMs >= WAIT_LOG_MS) {
			waitLoggedMs = now;
			HalfCraft.LOG.info("HalfCraft: {} boat(s) wait for Half-Life's water around them", count);
		}
	}

	/** Minecraft y of Half-Life's water surface over the column of cell (x, y, z), or NaN where it has none. */
	public static double surfaceAt(int x, int y, int z) {
		return WaterColumns.surface(grid, probes, x, y, z);
	}

	/**
	 * Where a line from {@code from} to {@code to} first comes down onto Half-Life's water (a boat put on
	 * it, BoatItemMixin), or null when it doesn't; from under the water it never does.
	 */
	public static @Nullable Vec3 surfaceAlong(BlockGetter level, Vec3 from, Vec3 to) {
		if (grid == null) {
			return null;
		}
		int steps = Math.max(1, (int) Math.ceil(from.distanceTo(to) / RAY_STEP));
		Vec3 prev = from;
		for (int i = 0; i <= steps; i++) {
			Vec3 p = from.lerp(to, (double) i / steps);
			BlockPos cell = BlockPos.containing(p);
			double s = surfaceAt(cell.getX(), cell.getY(), cell.getZ());
			if (!Double.isNaN(s) && p.y <= s && level.getBlockState(cell).isAir()) {
				if (i == 0) {
					return null; // the eye is under it
				}
				// where the line crosses the surface between the last step and this one
				double t = prev.y > s ? (prev.y - s) / (prev.y - p.y) : 1.0;
				Vec3 at = prev.lerp(p, t);
				return new Vec3(at.x, s, at.z);
			}
			prev = p;
		}
		return null;
	}

	/** How much of this block (0..1) is under Half-Life's water; 0 above the surface. */
	public static float depthIn(BlockPos pos) {
		double s = surfaceAt(pos.getX(), pos.getY(), pos.getZ());
		if (Double.isNaN(s)) {
			return 0.0F;
		}
		double h = s - pos.getY();
		return h < 0.02 ? 0.0F : (float) Math.min(1.0, h);
	}

	/** True if Half-Life water reaches up into the box of block cells (inclusive). */
	public static boolean anyIn(int x0, int y0, int z0, int x1, int y1, int z1) {
		if (grid == null) {
			return false;
		}
		for (int x = x0; x <= x1; x++) {
			for (int z = z0; z <= z1; z++) {
				double s = surfaceAt(x, y1, z);
				if (!Double.isNaN(s) && s > y0) {
					return true;
				}
			}
		}
		return false;
	}

	/** Half-Life water in an otherwise empty (air) Minecraft cell, as a Minecraft fluid; null if none. */
	public static @Nullable FluidState fluidAt(BlockGetter level, BlockPos pos) {
		if (depthIn(pos) <= 0.0F || !level.getBlockState(pos).isAir()) {
			return null;
		}
		return Fluids.WATER.getSource(false);
	}

	/** {@code state}, Minecraft's block at pos, or a water block where it is air in Half-Life's water. */
	public static BlockState blockOrWater(BlockPos pos, BlockState state) {
		return grid != null && state.isAir() && depthIn(pos) > 0.0F ? Blocks.WATER.defaultBlockState() : state;
	}

	/** The exact water height in a cell only Half-Life fills (so floating matches its surface); -1 otherwise. */
	public static float substitutedHeight(BlockGetter level, BlockPos pos) {
		float depth = depthIn(pos);
		if (depth <= 0.0F || !level.getFluidState(pos).isEmpty() || !level.getBlockState(pos).isAir()) {
			return -1.0F;
		}
		return depth;
	}

	/**
	 * The same for a boat: how far Half-Life's surface is above the cell's floor, more than 1 in a cell
	 * under it. A boat takes its water level from the cell its bottom is in and expects the surface
	 * there, as a water block's always is (8/9 up). Half-Life's surface can sit low in its cell, so a
	 * floating boat's bottom hangs in the full cell below: capped at 1, its level would read up to a
	 * block too low, the boat would settle with its top at the surface, count as under water, sink and
	 * throw its rider out.
	 */
	public static float surfaceOver(BlockGetter level, BlockPos pos) {
		if (substitutedHeight(level, pos) < 0.0F) {
			return -1.0F;
		}
		return (float) (surfaceAt(pos.getX(), pos.getY(), pos.getZ()) - pos.getY());
	}
}

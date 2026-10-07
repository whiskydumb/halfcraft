package dev.halfcraft.mobs;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.mixin.PathNavigationRegionAccessor;
import dev.halfcraft.world.HostClip;
import dev.halfcraft.world.HostCollision;
import dev.halfcraft.world.NavGrid;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.resources.Identifier;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.pathfinder.Node;
import net.minecraft.world.level.pathfinder.Path;
import net.minecraft.world.level.pathfinder.PathType;
import net.minecraft.world.level.pathfinder.PathfindingContext;
import net.minecraft.world.level.pathfinder.WalkNodeEvaluator;
import net.minecraft.world.phys.Vec3;
import org.jspecify.annotations.Nullable;

/**
 * Minecraft's own pathfinding over Half-Life's maps: the mixins on vanilla's pathfinder hand their
 * questions here, and {@link NavGrid} answers them from Half-Life's collision voxels (HostCollision),
 * in the mirror world only. Every movement goal (chasing, shooting, following the owner, teleporting,
 * wandering) then works on Half-Life ground. Flying mobs get Half-Life's walls (the raw cell types)
 * but none of the floors.
 *
 * <p>Collision streams around the player only, so a region Half-Life hasn't described is no place to
 * walk to, and mobs standing in one hold still until it arrives (every Half-Life level load starts
 * the collision over). A region being resent swaps its cells one at a time (HostCollision.readRegion):
 * a search running meanwhile can see it half updated, which costs at most one odd path.
 */
public final class HostNav {
	private static final Identifier MIRROR = Identifier.fromNamespaceAndPath(HalfCraft.MOD_ID, "mirror");
	// a mob's target is looked for this far below where it is, on Half-Life ground
	private static final int MAX_SURFACE_DROP = 64;
	// a mob with a Minecraft block this many cells below its feet is over Minecraft's ground (jumping,
	// or dropping onto it), not waiting for Half-Life's
	private static final int MINECRAFT_SUPPORT_DEPTH = 4;
	private static final long PATH_LOG_MS = 1000;
	private static final long RARE_LOG_MS = 5000;

	/** Half-Life's voxels as the pathfinding grid sees them. */
	public static final NavGrid.Cells CELLS = new NavGrid.Cells() {
		@Override
		public long @Nullable [] layers(int x, int y, int z) {
			return HostCollision.layersAt(x, y, z);
		}

		@Override
		public boolean known(int x, int y, int z) {
			return HostCollision.isKnown(x, y, z);
		}

		@Override
		public int nav(int x, int y, int z) {
			return HostCollision.navAt(x, y, z);
		}

		@Override
		public boolean sky(int x, int y, int z) {
			return HostCollision.isSkyAt(x, y, z);
		}
	};

	private static final Map<String, Long> LAST_NOTES = new ConcurrentHashMap<>();
	// server thread only: mobs and things held this tick, and how many the last log line was about
	private static int held;
	private static int heldLogged;

	private HostNav() {
	}

	/** The mirror world, whose ground is Half-Life's (whether or not Half-Life has described it yet). */
	public static boolean inMirror(@Nullable Level level) {
		return level != null && level.dimensionTypeRegistration().is(MIRROR);
	}

	/** {@link #inMirror(Level)} for what a pathfinding search reads: the level, or its region of it. */
	public static boolean inMirror(BlockGetter getter) {
		if (getter instanceof Level level) {
			return inMirror(level);
		}
		return getter instanceof PathNavigationRegionAccessor region && inMirror(region.halfcraft$level());
	}

	/**
	 * A cell's own type, after ServerLevel's cache (which only Minecraft's block updates clear, and
	 * Half-Life's doors move): air in Half-Life's walls is blocked, thin railings are fences.
	 */
	public static PathType rawType(PathType vanilla, int x, int y, int z) {
		if (vanilla != PathType.OPEN) {
			return vanilla;
		}
		return switch (NavGrid.kind(HostCollision.navAt(x, y, z))) {
			case NavGrid.SOLID -> PathType.BLOCKED;
			case NavGrid.FENCE -> PathType.FENCE;
			default -> vanilla;
		};
	}

	/**
	 * A node's type: Half-Life's floor in the cell makes it walkable (the way a carpet does), and air
	 * Half-Life hasn't described is blocked. Blocked here, not as the raw type: a blocked raw type would
	 * make the cell above it a floor.
	 */
	public static PathType nodeType(PathType vanilla, PathfindingContext context, int x, int y, int z) {
		if (vanilla != PathType.OPEN) {
			return vanilla;
		}
		if (!HostCollision.isKnown(x, y, z)) {
			return PathType.BLOCKED;
		}
		if (NavGrid.kind(HostCollision.navAt(x, y, z)) == NavGrid.FLOOR) {
			return WalkNodeEvaluator.checkNeighbourBlocks(context, x, y, z, PathType.WALKABLE);
		}
		return vanilla;
	}

	/** Where a mob in this cell stands: on the higher of Minecraft's block below and Half-Life's ground. */
	public static double floorLevel(double vanilla, BlockPos pos) {
		double host = NavGrid.floorLevel(CELLS, pos.getX(), pos.getY(), pos.getZ());
		return Double.isNaN(host) ? vanilla : Math.max(vanilla, host);
	}

	/**
	 * The height a walking mob aims its feet at on its way to {@code target} (MoveControl jumps when it is
	 * more than a step above the mob): Half-Life's floor there. Vanilla takes the node's own height when
	 * the block below it is air, so mobs would jump up every stair and press into ledges they can't step.
	 */
	public static double groundY(double vanilla, Level level, Vec3 target) {
		BlockPos pos = BlockPos.containing(target);
		double host = NavGrid.floorLevel(CELLS, pos.getX(), pos.getY(), pos.getZ());
		// over a Minecraft block vanilla already took the higher of it and Half-Life's floor (getFloorLevel)
		return Double.isNaN(host) || !level.getBlockState(pos.below()).isAir() ? vanilla : host;
	}

	/** Half-Life's ground a mob in this cell stands on, or NaN. */
	public static double hostFloor(Level level, BlockPos pos) {
		return inMirror(level) ? NavGrid.floorLevel(CELLS, pos.getX(), pos.getY(), pos.getZ()) : Double.NaN;
	}

	/**
	 * The cell a target in the air above Half-Life ground stands in: the first floor below it (vanilla
	 * looks for a block, finds none in the void and picks the top of the world). Null when there is
	 * none Half-Life has described.
	 */
	public static @Nullable BlockPos surface(LevelChunk chunk, BlockPos pos) {
		int x = pos.getX(), z = pos.getZ();
		BlockPos.MutableBlockPos cell = new BlockPos.MutableBlockPos();
		for (int y = pos.getY(); y >= pos.getY() - MAX_SURFACE_DROP && HostCollision.isKnown(x, y, z); y--) {
			if (y < pos.getY() && !chunk.getBlockState(cell.set(x, y, z)).isAir()) {
				return new BlockPos(x, y + 1, z);
			}
			switch (NavGrid.kind(HostCollision.navAt(x, y, z))) {
				case NavGrid.FLOOR -> {
					return new BlockPos(x, y, z);
				}
				case NavGrid.SOLID, NavGrid.FENCE -> {
					return new BlockPos(x, y + 1, z);
				}
				default -> {
				}
			}
		}
		return null;
	}

	/** A mob may end a walk in this cell on Half-Life ground (a stroll's destination). */
	public static boolean stable(BlockPos pos) {
		return NavGrid.standable(CELLS, pos.getX(), pos.getY(), pos.getZ());
	}

	/** Half-Life's walls let a mob walk from one node to the next (see NavGrid.canCross). */
	public static boolean canCross(Mob mob, Node from, double floorFrom, Node to, double floorTo) {
		double middle = (int) (mob.getBbWidth() + 1.0F) * 0.5;
		return NavGrid.canCross(CELLS, from.x + middle, from.z + middle, floorFrom, to.x + middle, to.z + middle, floorTo, mob.getBbWidth(), mob.getBbHeight(),
			mob.maxUpStep());
	}

	/**
	 * Where a mob aims for when walking to a node: its middle, or the spot nearby it fits in when a
	 * Half-Life wall at the edge of the cell leaves no room there (the spot canCross planned with).
	 */
	public static Vec3 steer(Entity entity, Vec3 pos) {
		if (!(entity instanceof Mob mob) || !inMirror(mob.level())) {
			return pos;
		}
		double floor = NavGrid.floorLevel(CELLS, Mth.floor(pos.x), Mth.floor(pos.y), Mth.floor(pos.z));
		if (Double.isNaN(floor)) {
			return pos;
		}
		double[] spot = NavGrid.spot(CELLS, pos.x, floor, pos.z, mob.getBbWidth(), mob.getBbHeight(), mob.maxUpStep());
		return spot == null || spot[0] == 0.0 && spot[1] == 0.0 ? pos : pos.add(spot[0], 0.0, spot[1]);
	}

	/** Half-Life geometry stands between {@code looker}'s eye at {@code from} and {@code to} on {@code target}. */
	public static boolean blocksSight(Entity looker, Entity target, Vec3 from, Vec3 to) {
		if (!inMirror(looker.level()) || HostClip.cast(from, to) == null) {
			return false;
		}
		if (due("sight", RARE_LOG_MS)) {
			HalfCraft.LOG.info("HalfCraft: Half-Life geometry blocks {}'s sight of {}", looker.getName().getString(), target.getName().getString());
		}
		return true;
	}

	/** Half-Life geometry shades this cell from the sky (undead don't burn under it). */
	public static boolean roofed(Mob mob, BlockPos eye) {
		if (!inMirror(mob.level()) || !NavGrid.roofed(CELLS, eye.getX(), eye.getY(), eye.getZ())) {
			return false;
		}
		if (due("sun", RARE_LOG_MS)) {
			HalfCraft.LOG.info("HalfCraft: {} is under Half-Life's roof: no sunburn", mob.getName().getString());
		}
		return true;
	}

	/**
	 * Holds a mob, an item, a boat (anything but players and projectiles) still, with no AI and no
	 * gravity, while Half-Life hasn't described the ground around it: after every Half-Life level load
	 * the collision starts over, and the server keeps ticking while it streams back in, so what stood on
	 * Half-Life's floors fell through them. One on Minecraft's blocks, or in the air just above them
	 * (jumping), is left alone.
	 */
	public static boolean holdUntilGroundKnown(Entity entity) {
		Level level = entity.level();
		if (level.isClientSide() || entity.isPassenger() || entity.isNoGravity() || !inMirror(level)) {
			return false;
		}
		int x = Mth.floor(entity.getX()), y = Mth.floor(entity.getY()), z = Mth.floor(entity.getZ());
		if (HostCollision.isKnown(x, y, z) && HostCollision.isKnown(x, y - 1, z)) {
			return false;
		}
		if (overMinecraftBlocks(level, entity)) {
			return false;
		}
		entity.setDeltaMovement(Vec3.ZERO);
		entity.resetFallDistance();
		held++;
		return true;
	}

	// a Minecraft block holds it up, or lies within a jump or a short drop below it in the air
	private static boolean overMinecraftBlocks(Level level, Entity entity) {
		if (!level.getBlockState(entity.getOnPos()).isAir()) {
			return true;
		}
		if (entity.onGround()) {
			return false;  // on Half-Life's ground, which a level load took away until it streams back
		}
		BlockPos.MutableBlockPos pos = entity.blockPosition().mutable();
		for (int i = 0; i <= MINECRAFT_SUPPORT_DEPTH; i++, pos.move(Direction.DOWN)) {
			if (!level.getBlockState(pos).isAir()) {
				return true;
			}
		}
		return false;
	}

	/** A path a mob in the mirror world found (a greppable line now and then, for live checks). */
	public static void pathFound(Mob mob, @Nullable Path path) {
		if (path != null && inMirror(mob.level()) && due("path", PATH_LOG_MS)) {
			HalfCraft.LOG.info("HalfCraft: {} paths over {} nodes to {} (reaches it: {})", mob.getName().getString(), path.getNodeCount(),
				path.getTarget().toShortString(), path.canReach());
		}
	}

	/** End of a server tick: says when mobs and things start or stop waiting for Half-Life's ground. */
	static void endTick() {
		int count = held;
		held = 0;
		if ((count == 0) == (heldLogged == 0)) {
			return;
		}
		if (count > 0) {
			HalfCraft.LOG.info("HalfCraft: {} mobs and things hold still until Half-Life's ground around them streams in", count);
		} else {
			HalfCraft.LOG.info("HalfCraft: Half-Life's ground is back: mobs and things move again");
		}
		heldLogged = count;
	}

	// a log line of this kind is due (at most one every intervalMs): checks that run every tick stay quiet
	private static boolean due(String key, long intervalMs) {
		long now = System.currentTimeMillis();
		Long last = LAST_NOTES.get(key);
		if (last != null && now - last < intervalMs) {
			return false;
		}
		LAST_NOTES.put(key, now);
		return true;
	}
}

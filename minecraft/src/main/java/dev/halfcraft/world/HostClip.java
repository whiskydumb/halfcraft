package dev.halfcraft.world;

import dev.halfcraft.mobs.HostNav;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.util.Mth;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;

/**
 * Makes Minecraft ray casts (arrows and other projectiles, the crosshair pick) hit Half-Life's exact
 * collision triangles. Vanilla still clips against real Minecraft blocks; whichever is nearer wins.
 */
public final class HostClip {
	private HostClip() {
	}

	public enum Use {
		/** A projectile: the hit cell is the one the surface is in (it sticks there). */
		PROJECTILE,
		/** The player's crosshair: the hit cell is where a block placed against the surface goes. */
		PICK
	}

	private static final ThreadLocal<List<HostTri>> SCRATCH = ThreadLocal.withInitial(ArrayList::new);

	public static BlockHitResult refine(Vec3 from, Vec3 to, BlockHitResult vanilla, Use use) {
		HostRay.Hit hit = cast(from, to);
		if (hit == null) {
			return vanilla;
		}
		Vec3 location = new Vec3(hit.x(), hit.y(), hit.z());
		if (vanilla.getType() != HitResult.Type.MISS && from.distanceToSqr(vanilla.getLocation()) <= from.distanceToSqr(location)) {
			return vanilla;
		}
		Direction face = Direction.values()[HostRay.dominantFace(hit.nx(), hit.ny(), hit.nz())];
		int[] cell = use == Use.PICK ? HostRay.placementCell(hit) : HostRay.surfaceCell(hit);
		return new HostHitResult(location, face, new BlockPos(cell[0], cell[1], cell[2]), hit);
	}

	/** How far short of the edge of the described space a stopped projectile stays (blocks). */
	private static final double EDGE_INSET = 1.0E-3;
	/** How high over the described space a projectile may fly on (regions): open sky as far as Minecraft can tell. */
	private static final int SKY_REGIONS = 16;
	/** Where a projectile may be: space Half-Life has described, and the sky over it. */
	private static final RegionEdge.Known OPEN = RegionEdge.withSky(
		(rx, ry, rz) -> HostCollision.isKnown(rx * HostCollision.REGION_SIZE, ry * HostCollision.REGION_SIZE, rz * HostCollision.REGION_SIZE),
		SKY_REGIONS);

	/**
	 * {@link #refine} for a projectile in the mirror world, which also stops where it would leave the
	 * space Half-Life has described (collision streams about 64 blocks around the player, and only a
	 * few regions up and down). Past that it would fly through Half-Life's walls and fall out of the
	 * map: it meets an {@link EdgeHitResult} instead (see ProjectileMixin). Over that space it flies on,
	 * so a high shot comes back down.
	 */
	public static BlockHitResult refineProjectile(Level level, Vec3 from, Vec3 to, BlockHitResult vanilla) {
		BlockHitResult hit = refine(from, to, vanilla, Use.PROJECTILE);
		if (!HostCollision.active() || !HostNav.inMirror(level)) {
			return hit;
		}
		RegionEdge.Crossing edge = RegionEdge.first(from.x, from.y, from.z, to.x, to.y, to.z, HostCollision.REGION_SIZE, OPEN);
		if (edge == null) {
			return hit;
		}
		Vec3 at = from.lerp(to, edge.t());
		if (hit.getType() != HitResult.Type.MISS && from.distanceToSqr(hit.getLocation()) <= from.distanceToSqr(at)) {
			return hit;
		}
		Vec3 way = to.subtract(from).normalize();
		Direction.Axis axis = Direction.Axis.values()[edge.axis()];
		Direction face = Direction.fromAxisAndDirection(axis, edge.step() > 0 ? Direction.AxisDirection.NEGATIVE : Direction.AxisDirection.POSITIVE);
		return new EdgeHitResult(at.subtract(way.scale(EDGE_INSET)), face, BlockPos.containing(at.add(way.scale(EDGE_INSET))));
	}

	/** Whether a projectile at (x, y, z) in the mirror world is somewhere it may be (see {@link #refineProjectile}). */
	public static boolean open(Level level, double x, double y, double z) {
		int size = HostCollision.REGION_SIZE;
		return HostCollision.active() && HostNav.inMirror(level)
			&& OPEN.region(Math.floorDiv(Mth.floor(x), size), Math.floorDiv(Mth.floor(y), size), Math.floorDiv(Mth.floor(z), size));
	}

	/** Nearest Half-Life triangle hit on the segment, or null. */
	public static HostRay.Hit cast(Vec3 from, Vec3 to) {
		List<HostTri> tris = SCRATCH.get();
		tris.clear();
		HostCollision.trianglesNear(new AABB(from, to).inflate(0.01), tris);
		if (tris.isEmpty()) {
			return null;
		}
		HostRay.Hit hit = HostRay.cast(tris, from.x, from.y, from.z, to.x, to.y, to.z);
		tris.clear();
		return hit;
	}

	/** A hit on Half-Life geometry (not a Minecraft block). Keeps the exact surface normal and triangle. */
	public static final class HostHitResult extends BlockHitResult {
		public final double nx, ny, nz;
		public final HostRay.Hit hit;

		public HostHitResult(Vec3 location, Direction direction, BlockPos pos, HostRay.Hit hit) {
			super(location, direction, pos, false);
			this.nx = hit.nx();
			this.ny = hit.ny();
			this.nz = hit.nz();
			this.hit = hit;
		}
	}

	/**
	 * A projectile reaching space Half-Life hasn't described yet: {@code getLocation} is just short of
	 * it, {@code getBlockPos} the first cell past it. Nothing is there to hit, so it isn't one.
	 */
	public static final class EdgeHitResult extends BlockHitResult {
		public EdgeHitResult(Vec3 location, Direction direction, BlockPos pos) {
			super(location, direction, pos, false);
		}

		/** Whether the projectile went down out of the described space: the ground under it isn't known yet. */
		public boolean below() {
			return this.getDirection() == Direction.UP;
		}
	}
}

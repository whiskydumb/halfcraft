package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.llamalad7.mixinextras.sugar.Local;
import dev.halfcraft.combat.HostBlasts;
import dev.halfcraft.link.HostLink;
import dev.halfcraft.link.Proto;
import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.world.BlastRays;
import dev.halfcraft.world.HostCollision;
import dev.halfcraft.world.HostTri;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.ServerExplosion;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Minecraft explosions in the host's world. Half-Life's walls shelter what's behind them: bodies (the
 * exposure Minecraft scales a blast's damage and push by) and blocks (its block-breaking rays stop at
 * them). And the host feels each explosion too: its own blast hurts its people and throws its props.
 * Only in the mirror dimension: elsewhere there is no Half-Life.
 */
@Mixin(ServerExplosion.class)
public abstract class ServerExplosionMixin {
	/** Block rays travel at most radius * 1.3 / 0.225 steps of 0.3 blocks: a little over 1.73 radii. */
	@Unique
	private static final double HALFCRAFT$RAY_REACH_RADII = 1.8;
	// getSeenPercent's triangles, one sample point at a time
	@Unique
	private static final ThreadLocal<List<HostTri>> HALFCRAFT$SEEN_TRIS = ThreadLocal.withInitial(ArrayList::new);

	// Half-Life's triangles around this explosion, while its block rays are cast
	@Unique
	private final List<HostTri> halfcraft$tris = new ArrayList<>();
	// the ray being stepped (its direction) and how far it gets before Half-Life's geometry
	@Unique
	private double halfcraft$rayX = Double.NaN, halfcraft$rayY, halfcraft$rayZ, halfcraft$rayReach;

	@Inject(method = "calculateExplodedPositions", at = @At("HEAD"))
	private void halfcraft$gatherHostTris(CallbackInfoReturnable<List<BlockPos>> cir) {
		ServerExplosion self = (ServerExplosion) (Object) this;
		this.halfcraft$tris.clear();
		this.halfcraft$rayX = Double.NaN;
		if (!HostNav.inMirror(self.level())) {
			return;
		}
		HostCollision.trianglesNear(new AABB(self.center(), self.center()).inflate(self.radius() * HALFCRAFT$RAY_REACH_RADII), this.halfcraft$tris);
	}

	@Inject(method = "calculateExplodedPositions", at = @At("RETURN"))
	private void halfcraft$dropHostTris(CallbackInfoReturnable<List<BlockPos>> cir) {
		this.halfcraft$tris.clear();
	}

	/** A block ray ends where it meets Half-Life's geometry, as at the edge of the world. */
	@WrapOperation(
		method = "calculateExplodedPositions",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/server/level/ServerLevel;isInWorldBounds(Lnet/minecraft/core/BlockPos;)Z")
	)
	private boolean halfcraft$stopAtHostWalls(ServerLevel level, BlockPos pos, Operation<Boolean> original, @Local(ordinal = 0) double xd,
		@Local(ordinal = 1) double yd, @Local(ordinal = 2) double zd, @Local(ordinal = 4) double xp, @Local(ordinal = 5) double yp,
		@Local(ordinal = 6) double zp) {
		if (!original.call(level, pos)) {
			return false;
		}
		if (this.halfcraft$tris.isEmpty()) {
			return true;
		}
		Vec3 center = ((ServerExplosion) (Object) this).center();
		if (xd != this.halfcraft$rayX || yd != this.halfcraft$rayY || zd != this.halfcraft$rayZ) {
			this.halfcraft$rayX = xd;
			this.halfcraft$rayY = yd;
			this.halfcraft$rayZ = zd;
			double max = ((ServerExplosion) (Object) this).radius() * HALFCRAFT$RAY_REACH_RADII;
			this.halfcraft$rayReach = BlastRays.reach(this.halfcraft$tris, center.x, center.y, center.z, xd, yd, zd, max);
		}
		double dx = xp - center.x, dy = yp - center.y, dz = zp - center.z;
		return dx * dx + dy * dy + dz * dz <= this.halfcraft$rayReach * this.halfcraft$rayReach;
	}

	/** A body behind a Half-Life wall is out of the blast's sight, as behind a block. */
	@WrapOperation(
		method = "getSeenPercent",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;")
	)
	private static BlockHitResult halfcraft$seeHostWalls(Level level, ClipContext context, Operation<BlockHitResult> original) {
		BlockHitResult vanilla = original.call(level, context);
		if (vanilla.getType() != HitResult.Type.MISS || !HostNav.inMirror(level)) {
			return vanilla;
		}
		// vanilla casts from the body's point to the explosion's centre
		Vec3 body = context.getFrom(), center = context.getTo();
		List<HostTri> tris = HALFCRAFT$SEEN_TRIS.get();
		tris.clear();
		HostCollision.trianglesNear(new AABB(body, center).inflate(BlastRays.LIFT + 0.01), tris);
		if (tris.isEmpty() || !BlastRays.hidden(tris, center.x, center.y, center.z, body.x, body.y, body.z)) {
			return vanilla;
		}
		return new BlockHitResult(center, Direction.UP, BlockPos.containing(center), false);
	}

	@Inject(method = "explode", at = @At("RETURN"))
	private void halfcraft$tellHost(CallbackInfoReturnable<Integer> cir) {
		ServerExplosion self = (ServerExplosion) (Object) this;
		if (!HostLink.active() || HostBlasts.isWindBurst(self) || !HostNav.inMirror(self.level())) {
			return;
		}
		var center = self.center();
		HostLink.pushEvent(Proto.EV_EXPLOSION, 0, (float) center.x, (float) center.y, (float) center.z, self.radius(), 0, 0, HostBlasts.culpritId(self));
	}
}

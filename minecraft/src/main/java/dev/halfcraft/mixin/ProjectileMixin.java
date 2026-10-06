package dev.halfcraft.mixin;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.world.HostClip;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.entity.projectile.ProjectileDeflection;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Minecraft ignores projectile hits on air, and to Minecraft a Half-Life wall is air. Treat a hit on
 * Half-Life geometry as a real hit: arrows stick in it, snowballs and eggs break on it.
 *
 * <p>Half-Life's collision streams only about 64 blocks around the player, and past it a projectile
 * would fly through Half-Life's walls and fall out of the map. At that edge it loses its way forward
 * and drops onto what Half-Life has described below it instead: arrows and tridents stick there (a
 * loyalty trident then comes back), pearls land, bobbers fall in. Projectiles without gravity
 * (fireballs, wind charges) can't drop, so they hit the edge like a wall. One falling out of the
 * bottom of the described space has nothing known to land on, so it stops there as on a floor: an
 * arrow stays until the ground under it streams in (AbstractArrowMixin), a loyalty trident comes back,
 * and anything else hits that floor (a pearl takes its thrower there, to fall on from it).
 */
@Mixin(Projectile.class)
public abstract class ProjectileMixin {
	@Unique
	private boolean halfcraft$edgeLogged;

	@Shadow
	protected abstract void onHit(HitResult hitResult);

	@Inject(method = "hitTargetOrDeflectSelf", at = @At("HEAD"), cancellable = true)
	private void halfcraft$hitHost(HitResult hitResult, CallbackInfoReturnable<ProjectileDeflection> cir) {
		if (hitResult instanceof HostClip.EdgeHitResult edge) {
			this.halfcraft$atEdge(edge);
			cir.setReturnValue(ProjectileDeflection.NONE);
		} else if (hitResult instanceof HostClip.HostHitResult) {
			this.onHit(hitResult);
			cir.setReturnValue(ProjectileDeflection.NONE);
		}
	}

	@Unique
	private void halfcraft$atEdge(HostClip.EdgeHitResult edge) {
		Projectile self = (Projectile) (Object) this;
		boolean drops = !edge.below() && self.getGravity() > 0.0;
		if (!this.halfcraft$edgeLogged && !self.level().isClientSide()) {
			this.halfcraft$edgeLogged = true;
			Vec3 at = edge.getLocation();
			HalfCraft.LOG.info("HalfCraft: {} reached the edge of Half-Life's collision at {} {} {}: {}", self.getType().toShortString(),
				String.format("%.1f", at.x), String.format("%.1f", at.y), String.format("%.1f", at.z),
				drops ? "drops" : edge.below() ? "stops on it" : "stops there");
		}
		if (drops) {
			self.setDeltaMovement(0.0, Math.min(self.getDeltaMovement().y, 0.0), 0.0);
		} else {
			this.onHit(edge);
		}
	}
}

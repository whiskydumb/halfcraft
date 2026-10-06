package dev.halfcraft.mixin;

import dev.halfcraft.mobs.HostNav;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Items, boats, falling blocks and the like hold still while Half-Life's ground under them streams in
 * (see HostNav.holdUntilGroundKnown), as mobs do (MobHoldMixin). Players move themselves, and
 * projectiles drop at the streamed volume's edge (RegionEdge).
 */
@Mixin(Entity.class)
public abstract class EntityHoldMixin {
	@Inject(method = "move", at = @At("HEAD"), cancellable = true)
	private void halfcraft$waitForHostGround(MoverType type, Vec3 movement, CallbackInfo ci) {
		Entity self = (Entity) (Object) this;
		if (!(self instanceof Player) && !(self instanceof Mob) && !(self instanceof Projectile) && HostNav.holdUntilGroundKnown(self)) {
			ci.cancel();
		}
	}
}

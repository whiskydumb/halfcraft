package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import dev.halfcraft.mobs.HostNav;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Half-Life's walls block sight like Minecraft's blocks do: a skeleton doesn't shoot at the player
 * through them, and mobs don't spot targets behind them.
 */
@Mixin(LivingEntity.class)
public abstract class LineOfSightMixin {
	@ModifyReturnValue(
		method = "hasLineOfSight(Lnet/minecraft/world/entity/Entity;Lnet/minecraft/world/level/ClipContext$Block;Lnet/minecraft/world/level/ClipContext$Fluid;D)Z",
		at = @At("RETURN")
	)
	private boolean halfcraft$hostWallsBlockSight(boolean seen, Entity target, ClipContext.Block block, ClipContext.Fluid fluid, double targetEyeY) {
		if (!seen) {
			return false;
		}
		LivingEntity self = (LivingEntity) (Object) this;
		return !HostNav.blocksSight(self, target, new Vec3(self.getX(), self.getEyeY(), self.getZ()), new Vec3(target.getX(), targetEyeY, target.getZ()));
	}
}

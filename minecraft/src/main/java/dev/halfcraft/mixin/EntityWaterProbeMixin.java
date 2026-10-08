package dev.halfcraft.mixin;

import dev.halfcraft.world.HostWater;
import net.minecraft.world.entity.Entity;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Mobs and dropped items off the player's water grid ask for Half-Life's water where they are
 * (HostWater.trackLoose), server side, where their physics run.
 */
@Mixin(Entity.class)
public abstract class EntityWaterProbeMixin {
	@Inject(method = "baseTick", at = @At("HEAD"))
	private void halfcraft$askForWater(CallbackInfo ci) {
		Entity self = (Entity) (Object) this;
		if (!self.level().isClientSide()) {
			HostWater.trackLoose(self);
		}
	}
}

package dev.halfcraft.mixin;

import dev.halfcraft.mobs.HostNav;
import net.minecraft.world.entity.Mob;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/** Mobs hold still while Half-Life's ground under them streams in (see HostNav.holdUntilGroundKnown). */
@Mixin(Mob.class)
public abstract class MobHoldMixin {
	@Inject(method = "aiStep", at = @At("HEAD"), cancellable = true)
	private void halfcraft$waitForHostGround(CallbackInfo ci) {
		if (HostNav.holdUntilGroundKnown((Mob) (Object) this)) {
			ci.cancel();
		}
	}
}

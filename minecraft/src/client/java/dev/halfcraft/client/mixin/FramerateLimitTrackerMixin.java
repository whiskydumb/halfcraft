package dev.halfcraft.client.mixin;

import com.mojang.blaze3d.platform.FramerateLimitTracker;
import dev.halfcraft.client.HostClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** HostClient.paceFrame() locks us to Half-Life's frame rate; don't let MC throttle on its own. */
@Mixin(FramerateLimitTracker.class)
public abstract class FramerateLimitTrackerMixin {
	@Inject(method = "getFramerateLimit", at = @At("HEAD"), cancellable = true)
	private void halfcraft$unlimited(CallbackInfoReturnable<Integer> cir) {
		if (HostClient.linked()) {
			cir.setReturnValue(260);
		}
	}
}

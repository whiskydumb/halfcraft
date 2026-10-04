package dev.halfcraft.client.mixin;

import com.mojang.blaze3d.platform.Window;
import dev.halfcraft.client.HostClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** The MC window is hidden while linked; Half-Life has the real focus, so pretend we do too. */
@Mixin(Window.class)
public abstract class WindowMixin {
	@Inject(method = "isFocused", at = @At("HEAD"), cancellable = true)
	private void halfcraft$focused(CallbackInfoReturnable<Boolean> cir) {
		if (HostClient.tookOver()) {
			// Focused while Half-Life is connected; if Half-Life goes away, act unfocused so MC
			// never tries to grab the (hidden) mouse.
			cir.setReturnValue(HostClient.linked());
		}
	}

	@Inject(method = "isIconified", at = @At("HEAD"), cancellable = true)
	private void halfcraft$notIconified(CallbackInfoReturnable<Boolean> cir) {
		if (HostClient.linked()) {
			cir.setReturnValue(false);
		}
	}
}

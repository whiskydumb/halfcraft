package dev.halfcraft.client.mixin;

import com.mojang.blaze3d.platform.Window;
import dev.halfcraft.client.HostClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.ModifyVariable;
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

	/**
	 * The overlay is drawn at Half-Life's viewport over a divisor and scaled back up (hc_overlay_scale): a
	 * gui scale the player set is divided alike, so the hud stays the size it was. The automatic one
	 * follows the smaller window by itself (Half-Life only picks a divisor it halves by).
	 */
	@ModifyVariable(method = "calculateScale", at = @At("HEAD"), argsOnly = true, ordinal = 0)
	private int halfcraft$divideSetScale(int guiScale) {
		int divisor = HostClient.linked() ? HostClient.overlayDivisor() : 1;
		return guiScale > 0 && divisor > 1 ? Math.max(1, Math.round(guiScale / (float) divisor)) : guiScale;
	}
}

package dev.halfcraft.client.mixin;

import com.mojang.blaze3d.platform.InputConstants;
import com.mojang.blaze3d.platform.Window;
import dev.halfcraft.client.InputBridge;
import dev.halfcraft.client.HostClient;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Keyboard state and mouse capture come from Half-Life while linked, not from SDL. */
@Mixin(InputConstants.class)
public abstract class InputConstantsMixin {
	@Inject(method = "isKeyDown", at = @At("HEAD"), cancellable = true)
	private static void halfcraft$isKeyDown(int key, CallbackInfoReturnable<Boolean> cir) {
		if (HostClient.tookOver()) {
			cir.setReturnValue(InputBridge.isKeyDown(key));
		}
	}

	@Inject(method = "grabMouse", at = @At("HEAD"), cancellable = true)
	private static void halfcraft$grabMouse(Window window, double xpos, double ypos, CallbackInfo ci) {
		if (HostClient.tookOver()) {
			ci.cancel();
		}
	}

	@Inject(method = "releaseMouse", at = @At("HEAD"), cancellable = true)
	private static void halfcraft$releaseMouse(Window window, double xpos, double ypos, CallbackInfo ci) {
		if (HostClient.tookOver()) {
			ci.cancel();
		}
	}
}

package dev.halfcraft.client.mixin;

import dev.halfcraft.client.HostScreenshots;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * The screenshot key while Half-Life is linked: Minecraft's framebuffer holds only its hand and HUD,
 * so Half-Life's finished frame is saved instead (HostScreenshots).
 */
@Mixin(Screenshot.class)
public abstract class ScreenshotMixin {
	@Inject(method = "grab(Lnet/minecraft/client/Minecraft;Z)V", at = @At("HEAD"), cancellable = true)
	private static void halfcraft$askHalfLife(Minecraft minecraft, boolean debugPanorama, CallbackInfo ci) {
		if (HostScreenshots.request(minecraft)) {
			ci.cancel();
		}
	}
}

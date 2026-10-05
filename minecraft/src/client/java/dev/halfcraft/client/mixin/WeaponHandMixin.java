package dev.halfcraft.client.mixin;

import com.mojang.renderpearl.api.textures.GpuTextureView;
import dev.halfcraft.client.HostClient;
import dev.halfcraft.weapon.HostWeapons;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.client.renderer.state.level.CameraRenderState;
import net.minecraft.client.renderer.state.level.PlayerRenderState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Half-Life's viewmodel is the hand while one of its weapons is held: Minecraft draws neither its
 * hands nor what they hold then (first person; the third-person body still holds the item).
 */
@Mixin(GameRenderer.class)
public abstract class WeaponHandMixin {
	@Inject(method = "renderItemInHand", at = @At("HEAD"), cancellable = true)
	private void halfcraft$hideHands(CameraRenderState camera, PlayerRenderState player, GpuTextureView target, CallbackInfo ci) {
		Minecraft minecraft = Minecraft.getInstance();
		if (HostClient.linked() && minecraft.player != null && HostWeapons.isStandIn(minecraft.player.getMainHandItem())) {
			ci.cancel();
		}
	}
}

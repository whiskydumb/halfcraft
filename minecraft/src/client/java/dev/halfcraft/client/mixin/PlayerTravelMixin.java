package dev.halfcraft.client.mixin;

import dev.halfcraft.client.HostTakeover;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * The local player's movement each tick, where Half-Life moves it besides Minecraft's own physics
 * ({@link HostTakeover}): instead of it while Half-Life has the player, and Half-Life's pushes after it.
 */
@Mixin(Player.class)
public abstract class PlayerTravelMixin {
	@Inject(method = "travel", at = @At("HEAD"), cancellable = true)
	private void halfcraft$followHalfLife(Vec3 input, CallbackInfo ci) {
		if ((Object) this instanceof LocalPlayer player && HostTakeover.follow(player)) {
			ci.cancel();
		}
	}

	@Inject(method = "travel", at = @At("TAIL"))
	private void halfcraft$pushedByHalfLife(Vec3 input, CallbackInfo ci) {
		if ((Object) this instanceof LocalPlayer player) {
			HostTakeover.push(player);
		}
	}
}

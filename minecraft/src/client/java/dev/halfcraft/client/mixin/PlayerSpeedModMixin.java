package dev.halfcraft.client.mixin;

import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import dev.halfcraft.client.HostSpeed;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.phys.Vec2;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Half-Life's player_speedmod slows Minecraft's player too (a dazed walk, the teleporter, the G-Man's
 * last scene): its walking input is scaled the way sneaking scales it, so its field of view stays put
 * as a slowness attribute's wouldn't.
 */
@Mixin(LocalPlayer.class)
public abstract class PlayerSpeedModMixin {
	@ModifyReturnValue(method = "modifyInput", at = @At("RETURN"))
	private Vec2 halfcraft$hostSpeedMod(Vec2 input) {
		float factor = HostSpeed.factor();
		return factor == 1.0F ? input : input.scale(factor);
	}
}

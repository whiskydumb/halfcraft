package dev.halfcraft.client.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.client.DuckJump;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.Pose;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/** The local player's pose changes go through {@link DuckJump}: crouching in the air pulls the legs up. */
@Mixin(Player.class)
public abstract class PlayerDuckJumpMixin {
	@WrapOperation(method = "updatePlayerPose", at = @At(value = "INVOKE", target = "Lnet/minecraft/world/entity/player/Player;setPose(Lnet/minecraft/world/entity/Pose;)V"))
	private void halfcraft$duckJump(Player self, Pose pose, Operation<Void> original) {
		if (self instanceof LocalPlayer player) {
			DuckJump.setPose(player, pose, to -> original.call(self, to));
		} else {
			original.call(self, pose);
		}
	}
}

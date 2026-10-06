package dev.halfcraft.mixin;

import dev.halfcraft.world.HostDuck;
import net.minecraft.world.entity.EntityDimensions;
import net.minecraft.world.entity.Pose;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Half-Life's ceilings keep a crouching player down ({@link HostDuck}). Minecraft picks the pose (and
 * whether letting go of sneak stands the player up) by whether its box fits among blocks, and the
 * player's box doesn't collide with Half-Life's voxels: it collides with the exact triangles instead.
 */
@Mixin(Player.class)
public abstract class PlayerDuckMixin {
	@Inject(method = "canPlayerFitWithinBlocksAndEntitiesWhen", at = @At("RETURN"), cancellable = true)
	private void halfcraft$underHostCeilings(Pose pose, CallbackInfoReturnable<Boolean> cir) {
		Player self = (Player) (Object) this;
		if (cir.getReturnValueZ() && HostDuck.applies(self)) {
			EntityDimensions size = self.getDimensions(pose);
			cir.setReturnValue(HostDuck.headroom(self.getX(), self.getY(), self.getZ(), size.width() / 2.0, size.height()));
		}
	}
}

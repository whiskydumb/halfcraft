package dev.halfcraft.mixin;

import dev.halfcraft.world.HostDuck;
import net.minecraft.world.entity.Avatar;
import net.minecraft.world.entity.EntityDimensions;
import net.minecraft.world.entity.Pose;
import net.minecraft.world.entity.player.Player;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** A crouching player in the mirror world is as tall as Source's ducked one ({@link HostDuck}). */
@Mixin(Avatar.class)
public abstract class AvatarDuckMixin {
	@Inject(method = "getDefaultDimensions", at = @At("RETURN"), cancellable = true)
	private void halfcraft$duckedHull(Pose pose, CallbackInfoReturnable<EntityDimensions> cir) {
		if (pose == Pose.CROUCHING && (Object) this instanceof Player player && HostDuck.applies(player)) {
			cir.setReturnValue(HostDuck.DIMENSIONS);
		}
	}
}

package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.world.HostWater;
import net.minecraft.core.BlockPos;
import net.minecraft.core.dispenser.BoatDispenseItemBehavior;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.material.FluidState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * A dispenser puts a boat onto Half-Life's water in front of it (or under the air in front of it), as
 * onto Minecraft's, instead of dropping the boat as an item.
 */
@Mixin(BoatDispenseItemBehavior.class)
public abstract class BoatDispenseItemBehaviorMixin {
	@WrapOperation(
		method = "execute",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/server/level/ServerLevel;getFluidState(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/material/FluidState;")
	)
	private FluidState halfcraft$hostWater(ServerLevel level, BlockPos pos, Operation<FluidState> original) {
		FluidState state = original.call(level, pos);
		if (state.isEmpty() && HostWater.active() && HostNav.inMirror(level)) {
			FluidState water = HostWater.fluidAt(level, pos);
			if (water != null) {
				return water;
			}
		}
		return state;
	}
}

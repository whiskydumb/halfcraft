package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.HalfCraft;
import dev.halfcraft.world.HostWater;
import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.projectile.FishingHook;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Fishing in Half-Life's water: the bobber floats on it and fish bite. The hook reads the water at
 * its own cell rather than through the entity fluid code, so those reads go through
 * {@link HostWater}, and so do the blocks it checks for open water (treasure) and for the trail of
 * an approaching fish.
 */
@Mixin(FishingHook.class)
public abstract class FishingHookMixin {
	@Shadow
	private int nibble;

	@Unique
	private boolean halfcraft$nibbling;

	@Inject(method = "tick", at = @At("HEAD"))
	private void halfcraft$askForWater(CallbackInfo ci) {
		HostWater.track((Entity) (Object) this);
	}

	@WrapOperation(
		method = "tick",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;getFluidState(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/material/FluidState;")
	)
	private FluidState halfcraft$hostWater(Level level, BlockPos pos, Operation<FluidState> original) {
		FluidState state = original.call(level, pos);
		if (state.isEmpty() && HostWater.active()) {
			FluidState water = HostWater.fluidAt(level, pos);
			if (water != null) {
				return water;
			}
		}
		return state;
	}

	@WrapOperation(
		method = "tick",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/world/level/material/FluidState;getHeight(Lnet/minecraft/world/level/BlockGetter;Lnet/minecraft/core/BlockPos;)F"
		)
	)
	private float halfcraft$hostWaterHeight(FluidState state, BlockGetter level, BlockPos pos, Operation<Float> original) {
		float height = HostWater.active() ? HostWater.substitutedHeight(level, pos) : -1.0F;
		return height >= 0.0F ? height : original.call(state, level, pos);
	}

	@WrapOperation(
		method = "catchingFish",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/server/level/ServerLevel;getBlockState(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/block/state/BlockState;"
		)
	)
	private BlockState halfcraft$hostWaterUnderFish(ServerLevel level, BlockPos pos, Operation<BlockState> original) {
		return HostWater.blockOrWater(pos, original.call(level, pos));
	}

	/** One line per bite in Half-Life's water: a fish has the bait, and reeling in now catches it. */
	@Inject(method = "catchingFish", at = @At("RETURN"))
	private void halfcraft$logBite(BlockPos pos, CallbackInfo ci) {
		boolean nibbling = this.nibble > 0;
		Entity self = (Entity) (Object) this;
		if (nibbling && !this.halfcraft$nibbling && HostWater.substitutedHeight(self.level(), pos) >= 0.0F) {
			HalfCraft.LOG.info("HalfCraft: a fish bites at the bobber in Half-Life's water at {}", pos.toShortString());
		}
		this.halfcraft$nibbling = nibbling;
	}

	@WrapOperation(
		method = "getOpenWaterTypeForBlock",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/world/level/Level;getBlockState(Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/level/block/state/BlockState;"
		)
	)
	private BlockState halfcraft$hostOpenWater(Level level, BlockPos pos, Operation<BlockState> original) {
		return HostWater.blockOrWater(pos, original.call(level, pos));
	}
}

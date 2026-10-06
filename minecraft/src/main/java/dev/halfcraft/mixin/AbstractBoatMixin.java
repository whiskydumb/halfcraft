package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.world.HostCollision;
import dev.halfcraft.world.HostWater;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.vehicle.boat.AbstractBoat;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FluidState;
import net.minecraft.world.phys.shapes.Shapes;
import net.minecraft.world.phys.shapes.VoxelShape;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Boats float on Half-Life's water and grip its ground. A boat reads the water around it itself
 * instead of through the entity fluid code, so its reads go through {@link HostWater} here: floating,
 * sinking under, the water level its rider sits at and fall damage. Its friction on land comes from
 * the block shapes under it, which on Half-Life ground are air, so a beached boat slid like on ice.
 */
@Mixin(AbstractBoat.class)
public abstract class AbstractBoatMixin {
	@Inject(method = "tick", at = @At("HEAD"))
	private void halfcraft$askForWater(CallbackInfo ci) {
		HostWater.track((Entity) (Object) this);
	}

	@WrapOperation(
		method = { "getWaterLevelAbove", "checkInWater", "isUnderwater", "checkFallDamage" },
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
		method = { "getWaterLevelAbove", "checkInWater", "isUnderwater" },
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/world/level/material/FluidState;getHeight(Lnet/minecraft/world/level/BlockGetter;Lnet/minecraft/core/BlockPos;)F"
		)
	)
	private float halfcraft$hostWaterHeight(FluidState state, BlockGetter level, BlockPos pos, Operation<Float> original) {
		float height = HostWater.active() ? HostWater.surfaceOver(level, pos) : -1.0F;
		return height >= 0.0F ? height : original.call(state, level, pos);
	}

	@WrapOperation(
		method = "getGroundFriction",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/world/level/block/state/BlockState;getCollisionShape(Lnet/minecraft/world/level/BlockGetter;Lnet/minecraft/core/BlockPos;)Lnet/minecraft/world/phys/shapes/VoxelShape;"
		)
	)
	private VoxelShape halfcraft$hostGround(BlockState state, BlockGetter level, BlockPos pos, Operation<VoxelShape> original) {
		VoxelShape shape = original.call(state, level, pos);
		VoxelShape host = HostCollision.shapeAt(pos);
		if (host == null) {
			return shape;
		}
		return shape.isEmpty() ? host : Shapes.or(shape, host);
	}
}

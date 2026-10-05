package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.HalfCraft;
import dev.halfcraft.mobs.HostNav;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.TamableAnimal;
import net.minecraft.world.level.pathfinder.PathType;
import net.minecraft.world.level.pathfinder.WalkNodeEvaluator;
import net.minecraft.world.phys.AABB;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * A pet left far behind teleports to its owner onto Half-Life ground too, standing on it: vanilla
 * puts it at the bottom of the cell, inside a Half-Life floor that sits higher in the cell, and
 * Minecraft's collision never pushes anything out of a shape, so it would drop through.
 */
@Mixin(TamableAnimal.class)
public abstract class PetTeleportMixin {
	@Inject(method = "canTeleportTo", at = @At("HEAD"), cancellable = true)
	private void halfcraft$ontoHostGround(BlockPos pos, CallbackInfoReturnable<Boolean> cir) {
		TamableAnimal self = (TamableAnimal) (Object) this;
		if (Double.isNaN(HostNav.hostFloor(self.level(), pos))) {
			return;
		}
		// a floor, or a solid under the cell: not the top of a railing
		if (!HostNav.stable(pos) || WalkNodeEvaluator.getPathTypeStatic(self, pos) != PathType.WALKABLE) {
			cir.setReturnValue(false);
			return;
		}
		double floor = WalkNodeEvaluator.getFloorLevel(self.level(), pos);
		AABB box = self.getBoundingBox().move(pos.getX() + 0.5 - self.getX(), floor - self.getY(), pos.getZ() + 0.5 - self.getZ());
		cir.setReturnValue(self.level().noCollision(self, box));
	}

	@WrapOperation(method = "maybeTeleportTo", at = @At(value = "INVOKE", target = "Lnet/minecraft/world/entity/TamableAnimal;snapTo(DDDFF)V"))
	private void halfcraft$snapOntoHostGround(TamableAnimal self, double x, double y, double z, float yRot, float xRot, Operation<Void> original) {
		BlockPos pos = BlockPos.containing(x, y, z);
		double standing = y;
		if (!Double.isNaN(HostNav.hostFloor(self.level(), pos))) {
			standing = WalkNodeEvaluator.getFloorLevel(self.level(), pos);
			HalfCraft.LOG.info("HalfCraft: {} teleported to its owner onto Half-Life ground at ({}, {}, {})", self.getName().getString(), String.format("%.2f", x),
				String.format("%.2f", standing), String.format("%.2f", z));
		}
		original.call(self, x, standing, z, yRot, xRot);
	}
}

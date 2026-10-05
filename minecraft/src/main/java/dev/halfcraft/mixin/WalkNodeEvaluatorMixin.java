package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import com.llamalad7.mixinextras.sugar.Local;
import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.mobs.HostNavContext;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.pathfinder.Node;
import net.minecraft.world.level.pathfinder.NodeEvaluator;
import net.minecraft.world.level.pathfinder.PathType;
import net.minecraft.world.level.pathfinder.PathfindingContext;
import net.minecraft.world.level.pathfinder.WalkNodeEvaluator;
import org.jspecify.annotations.Nullable;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Walking mobs path over Half-Life's ground: its floors are walkable nodes at their real height,
 * regions Half-Life hasn't described are no place to go, and the walls between two nodes' cells stop
 * the step from one to the other (see HostNav).
 */
@Mixin(WalkNodeEvaluator.class)
public abstract class WalkNodeEvaluatorMixin extends NodeEvaluator {
	@Shadow
	protected abstract double getFloorLevel(BlockPos pos);

	@ModifyReturnValue(
		method = "getPathTypeStatic(Lnet/minecraft/world/level/pathfinder/PathfindingContext;Lnet/minecraft/core/BlockPos$MutableBlockPos;)Lnet/minecraft/world/level/pathfinder/PathType;",
		at = @At("RETURN")
	)
	private static PathType halfcraft$hostFloor(PathType type, PathfindingContext context, BlockPos.MutableBlockPos pos) {
		return ((HostNavContext) context).halfcraft$inMirror() ? HostNav.nodeType(type, context, pos.getX(), pos.getY(), pos.getZ()) : type;
	}

	@ModifyReturnValue(method = "getFloorLevel(Lnet/minecraft/world/level/BlockGetter;Lnet/minecraft/core/BlockPos;)D", at = @At("RETURN"))
	private static double halfcraft$hostFloorLevel(double floor, BlockGetter level, BlockPos pos) {
		return HostNav.inMirror(level) ? HostNav.floorLevel(floor, pos) : floor;
	}

	@WrapOperation(
		method = "getNeighbors",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/world/level/pathfinder/WalkNodeEvaluator;findAcceptedNode(IIIIDLnet/minecraft/core/Direction;Lnet/minecraft/world/level/pathfinder/PathType;)Lnet/minecraft/world/level/pathfinder/Node;"
		)
	)
	private @Nullable Node halfcraft$hostWallsBetween(
		WalkNodeEvaluator self, int x, int y, int z, int jumps, double floor, Direction direction, PathType fromType, Operation<Node> original,
		@Local(argsOnly = true) Node from
	) {
		Node node = original.call(self, x, y, z, jumps, floor, direction, fromType);
		if (node == null || node.costMalus < 0.0F || !((HostNavContext) this.currentContext).halfcraft$inMirror()) {
			return node;
		}
		return HostNav.canCross(this.mob, from, floor, node, this.getFloorLevel(new BlockPos(node.x, node.y, node.z))) ? node : null;
	}
}

package dev.halfcraft.mixin;

import dev.halfcraft.world.FluidCells;
import dev.halfcraft.world.HostCollision;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.tags.FluidTags;
import net.minecraft.world.level.BlockGetter;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.material.FlowingFluid;
import net.minecraft.world.level.material.FluidState;
import org.jspecify.annotations.Nullable;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Water and lava settle on Half-Life's ground and run over it, never into it (see FluidCells):
 * <ul>
 * <li>a fluid lies on the floor under the middle of its cell and never flows down through it; with
 * only a wall in its cell, it falls past;</li>
 * <li>it moves into a cell only where nothing stands between it and that cell's middle (a wall
 * crossing the far side of the cell lets it in, so it reaches walls) and its surface there would be
 * above that cell's floor, so it runs downhill and over bumps but not uphill;</li>
 * <li>an empty cell under any geometry is under the ground (or inside a brush, or an overhang), so no
 * fluid flows sideways into it;</li>
 * <li>it never enters cells Half-Life hasn't described yet (far from the player).</li>
 * </ul>
 */
@Mixin(FlowingFluid.class)
public abstract class FlowingFluidMixin {
	// How far above the floors a fluid looks for walls between the middles of two cells (blocks).
	private static final double REACH_LIFT = 0.1;

	@Inject(method = "canPassThroughWall", at = @At("HEAD"), cancellable = true)
	private static void halfcraft$hostWall(
		Direction direction, BlockGetter level, BlockPos sourcePos, BlockState sourceState, BlockPos targetPos, BlockState targetState,
		CallbackInfoReturnable<Boolean> cir
	) {
		if (!targetState.isAir() || !HostCollision.active() || direction == Direction.UP) {
			return;
		}
		if (!HostCollision.isKnown(targetPos.getX(), targetPos.getY(), targetPos.getZ())) {
			refused("unknown region", direction, sourcePos, sourceState, targetPos, 0.0F);
			cir.setReturnValue(false);
			return;
		}
		float targetFloor = floor(targetPos);
		String why;
		if (direction == Direction.DOWN) {
			float height = height(sourceState);
			why = FluidCells.down(height, HostCollision.floorTop(sourcePos, height), layers(sourcePos), targetFloor, layers(targetPos));
		} else {
			float floor = floor(sourcePos);
			why = FluidCells.sideways(surface(sourceState), floor, targetFloor, layers(targetPos), layers(targetPos.above()),
				reachable(sourcePos, targetPos, Math.max(floor, targetFloor)));
		}
		if (why != null) {
			refused(why, direction, sourcePos, sourceState, targetPos, targetFloor);
			cir.setReturnValue(false);
		}
	}

	/** The floor under the middle of the cell (0..1), 0 without one. */
	private static float floor(BlockPos pos) {
		return HostCollision.floorTop(pos, 1.0F);
	}

	private static long @Nullable [] layers(BlockPos pos) {
		return HostCollision.layersAt(pos.getX(), pos.getY(), pos.getZ());
	}

	/**
	 * Whether nothing stands between the middle of the cell a fluid leaves (where the fluid is: open) and
	 * the middle of the one it flows into, a little above both floors.
	 */
	private static boolean reachable(BlockPos source, BlockPos target, float floor) {
		double y = target.getY() + Math.min(floor + REACH_LIFT, 1.0 - REACH_LIFT);
		return !HostCollision.blocked(source.getX() + 0.5, y, source.getZ() + 0.5, target.getX() + 0.5, y, target.getZ() + 0.5);
	}

	/** The fluid's own height in its cell (0..1). */
	private static float height(BlockState sourceState) {
		FluidState fluid = sourceState.getFluidState();
		return fluid.isEmpty() ? 1.0F : fluid.getOwnHeight();
	}

	/** Where the fluid's surface would be in the cell it flows into. */
	private static float surface(BlockState sourceState) {
		FluidState fluid = sourceState.getFluidState();
		if (fluid.isEmpty()) {
			return 0.8F; // Minecraft looking ahead for a slope: any cell a flow could reach
		}
		if (fluid.getValue(FlowingFluid.FALLING)) {
			return 7.0F / 9.0F; // a falling fluid spreads at level 7 where it lands
		}
		// One level less there (lava drops two outside the Nether).
		int drop = fluid.is(FluidTags.LAVA) ? 2 : 1;
		return Math.max(0, fluid.getAmount() - drop) / 9.0F;
	}

	private static long halfcraft$lastLog;
	private static int halfcraft$logged;

	private static void refused(String why, Direction direction, BlockPos sourcePos, BlockState sourceState, BlockPos targetPos, float ground) {
		long now = System.currentTimeMillis();
		if (now - halfcraft$lastLog > 1000) {
			halfcraft$lastLog = now;
			halfcraft$logged = 0;
		}
		if (halfcraft$logged++ < 4) {
			FluidState fluid = sourceState.getFluidState();
			dev.halfcraft.HalfCraft.LOG.info("HalfCraft: fluid flow refused ({}): {} -> {} going {}, fluid {} amount {}, Half-Life floor there {}", why,
				sourcePos.toShortString(), targetPos.toShortString(), direction, fluid.getType(), fluid.getAmount(), String.format("%.2f", ground));
		}
	}
}

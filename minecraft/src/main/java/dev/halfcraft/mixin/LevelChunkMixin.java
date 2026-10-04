package dev.halfcraft.mixin;

import dev.halfcraft.world.Rollback;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/** Every block change in the mirror world goes past the rollback's log (Rollback). */
@Mixin(LevelChunk.class)
public abstract class LevelChunkMixin {
	@Inject(method = "setBlockState", at = @At("HEAD"))
	private void halfcraft$before(BlockPos pos, BlockState state, int flags, CallbackInfoReturnable<BlockState> cir) {
		Rollback.beforeChange((LevelChunk) (Object) this, pos);
	}

	@Inject(method = "setBlockState", at = @At("RETURN"))
	private void halfcraft$after(BlockPos pos, BlockState state, int flags, CallbackInfoReturnable<BlockState> cir) {
		Rollback.afterChange((LevelChunk) (Object) this, pos);
	}
}

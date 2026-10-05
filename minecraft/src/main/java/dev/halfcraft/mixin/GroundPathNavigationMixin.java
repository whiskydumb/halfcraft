package dev.halfcraft.mixin;

import dev.halfcraft.mobs.HostNav;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.ai.navigation.GroundPathNavigation;
import net.minecraft.world.level.chunk.LevelChunk;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * A target standing on Half-Life ground is walked to where it stands. Vanilla looks down for a block,
 * finds only the mirror world's void and sends the mob to the top of the world instead.
 */
@Mixin(GroundPathNavigation.class)
public abstract class GroundPathNavigationMixin {
	@Inject(method = "findSurfacePosition", at = @At("HEAD"), cancellable = true)
	private void halfcraft$hostSurface(LevelChunk chunk, BlockPos pos, int reach, CallbackInfoReturnable<BlockPos> cir) {
		if (HostNav.inMirror(chunk.getLevel()) && chunk.getBlockState(pos).isAir()) {
			BlockPos surface = HostNav.surface(chunk, pos);
			cir.setReturnValue(surface != null ? surface : pos);
		}
	}
}

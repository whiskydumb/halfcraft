package dev.halfcraft.client.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.client.debug.HostDebugScreen;
import net.minecraft.client.gui.components.DebugScreenOverlay;
import net.minecraft.client.gui.components.debug.DebugScreenDisplayer;
import net.minecraft.client.gui.components.debug.DebugScreenEntry;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.chunk.LevelChunk;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * While Half-Life is linked the debug screen leaves out Minecraft's entries about the mirror world's
 * terrain (position, chunk, biome, heightmap, light): Half-Life's own entries say where the player
 * really is.
 */
@Mixin(DebugScreenOverlay.class)
public abstract class HostDebugOverlayMixin {
	@WrapOperation(
		method = "extractRenderState",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/client/gui/components/debug/DebugScreenEntry;display(Lnet/minecraft/client/gui/components/debug/DebugScreenDisplayer;Lnet/minecraft/world/level/Level;Lnet/minecraft/world/level/chunk/LevelChunk;Lnet/minecraft/world/level/chunk/LevelChunk;)V"
		)
	)
	private void halfcraft$leaveOutMirrorWorld(DebugScreenEntry entry, DebugScreenDisplayer displayer, Level level, LevelChunk clientChunk, LevelChunk serverChunk,
		Operation<Void> original) {
		if (!HostDebugScreen.hides(entry)) {
			original.call(entry, displayer, level, clientChunk, serverChunk);
		}
	}
}

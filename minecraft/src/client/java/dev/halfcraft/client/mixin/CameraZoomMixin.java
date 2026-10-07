package dev.halfcraft.client.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.client.HostClient;
import dev.halfcraft.link.HostLink;
import dev.halfcraft.world.HostClip;
import net.minecraft.client.Camera;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * The third-person camera (F5) pulls in against Half-Life's walls, terrain and trees as well as
 * Minecraft blocks, the way Minecraft's own camera does against blocks. In a Half-Life vehicle's
 * seat only against blocks: the vehicle round the seat is Half-Life collision too and would stop it
 * at the head (a Minecraft boat doesn't), so Half-Life pulls it in there, leaving the vehicle out.
 */
@Mixin(Camera.class)
public abstract class CameraZoomMixin {
	@WrapOperation(
		method = "getMaxZoom",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;")
	)
	private BlockHitResult halfcraft$zoomAgainstHost(Level level, ClipContext context, Operation<BlockHitResult> original) {
		HostLink.HostState sky = HostClient.sky();
		if (HostClient.linked() && sky.takeover() && sky.seated()) {
			return original.call(level, context);
		}
		return HostClip.refine(context.getFrom(), context.getTo(), original.call(level, context), HostClip.Use.PROJECTILE);
	}
}

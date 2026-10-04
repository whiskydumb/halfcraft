package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.world.HostClip;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * The crosshair targets Half-Life surfaces: blocks can be placed on terrain and walls, and NPCs behind a
 * wall can't be hit through it. A Half-Life hit points at the cell a placed block would occupy.
 */
@Mixin(Entity.class)
public abstract class EntityPickMixin {
	@WrapOperation(
		method = "pick",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;clip(Lnet/minecraft/world/level/ClipContext;)Lnet/minecraft/world/phys/BlockHitResult;")
	)
	private BlockHitResult halfcraft$pickHost(Level level, ClipContext context, Operation<BlockHitResult> original) {
		return HostClip.refine(context.getFrom(), context.getTo(), original.call(level, context), HostClip.Use.PICK);
	}
}

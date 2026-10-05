package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.mobs.HostNav;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.level.Level;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Undead burn only under Half-Life's open sky. The mirror world is stuck at noon with nothing above,
 * so to Minecraft every Half-Life ceiling is open sky.
 */
@Mixin(Mob.class)
public abstract class MobSunMixin {
	@WrapOperation(method = "isSunBurnTick", at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/Level;canSeeSky(Lnet/minecraft/core/BlockPos;)Z"))
	private boolean halfcraft$underHostRoof(Level level, BlockPos eye, Operation<Boolean> original) {
		return original.call(level, eye) && !HostNav.roofed((Mob) (Object) this, eye);
	}
}

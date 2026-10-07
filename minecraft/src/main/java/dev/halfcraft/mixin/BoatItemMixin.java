package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.world.HostWater;
import net.minecraft.core.BlockPos;
import net.minecraft.core.Direction;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.BoatItem;
import net.minecraft.world.level.ClipContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.BlockHitResult;
import net.minecraft.world.phys.HitResult;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * A boat goes onto Half-Life's water where the player looks at it, as onto Minecraft's: the boat's
 * look sees fluids, and Half-Life's water isn't one of Minecraft's, so the look went through it to the
 * bottom (out of reach: nothing happened).
 */
@Mixin(BoatItem.class)
public abstract class BoatItemMixin {
	@WrapOperation(
		method = "use",
		at = @At(
			value = "INVOKE",
			target = "Lnet/minecraft/world/item/BoatItem;getPlayerPOVHitResult(Lnet/minecraft/world/level/Level;Lnet/minecraft/world/entity/player/Player;Lnet/minecraft/world/level/ClipContext$Fluid;)Lnet/minecraft/world/phys/BlockHitResult;"
		)
	)
	private BlockHitResult halfcraft$onHostWater(Level level, Player player, ClipContext.Fluid fluid, Operation<BlockHitResult> original) {
		BlockHitResult hit = original.call(level, player, fluid);
		if (!HostWater.active() || !HostNav.inMirror(level)) {
			return hit;
		}
		Vec3 eye = player.getEyePosition();
		Vec3 end = hit.getType() == HitResult.Type.MISS ? eye.add(player.getViewVector(1.0F).scale(player.blockInteractionRange())) : hit.getLocation();
		Vec3 water = HostWater.surfaceAlong(level, eye, end);
		return water == null ? hit : new BlockHitResult(water, Direction.UP, BlockPos.containing(water), false);
	}
}

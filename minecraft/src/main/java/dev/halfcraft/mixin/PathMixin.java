package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import dev.halfcraft.mobs.HostNav;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.pathfinder.Path;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;

/**
 * A mob walks to the spot of each node it fits in: where a Half-Life wall at the edge of the cell
 * leaves no room in the middle, a little off it (the pathfinder planned the step with that spot).
 */
@Mixin(Path.class)
public abstract class PathMixin {
	@ModifyReturnValue(method = "getEntityPosAtNode", at = @At("RETURN"))
	private Vec3 halfcraft$fitBetweenHostWalls(Vec3 pos, Entity entity, int index) {
		return HostNav.steer(entity, pos);
	}
}

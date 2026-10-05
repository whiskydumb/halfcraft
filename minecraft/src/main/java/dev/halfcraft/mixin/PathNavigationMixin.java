package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import dev.halfcraft.mobs.HostNav;
import net.minecraft.core.BlockPos;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.ai.navigation.PathNavigation;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.pathfinder.Path;
import net.minecraft.world.phys.Vec3;
import org.jspecify.annotations.Nullable;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;

/**
 * Strolling mobs may end their walk on Half-Life ground, not only on top of a Minecraft block, and a
 * walking mob aims its feet at the height of Half-Life's floor ahead, so it jumps up ledges and not up
 * every stair.
 */
@Mixin(PathNavigation.class)
public abstract class PathNavigationMixin {
	@Shadow
	@Final
	protected Mob mob;

	@Shadow
	@Final
	protected Level level;

	@ModifyReturnValue(method = "isStableDestination", at = @At("RETURN"))
	private boolean halfcraft$hostGround(boolean stable, BlockPos pos) {
		return stable || HostNav.inMirror(this.level) && HostNav.stable(pos);
	}

	@ModifyReturnValue(method = "getGroundY(Lnet/minecraft/world/phys/Vec3;)D", at = @At("RETURN"))
	private double halfcraft$hostGroundY(double groundY, Vec3 target) {
		return HostNav.inMirror(this.level) ? HostNav.groundY(groundY, this.level, target) : groundY;
	}

	@ModifyReturnValue(method = "createPath(Ljava/util/Set;IZIF)Lnet/minecraft/world/level/pathfinder/Path;", at = @At("RETURN"))
	private @Nullable Path halfcraft$notePath(@Nullable Path path) {
		HostNav.pathFound(this.mob, path);
		return path;
	}
}

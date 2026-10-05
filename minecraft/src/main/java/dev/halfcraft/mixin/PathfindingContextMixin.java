package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.ModifyReturnValue;
import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.mobs.HostNavContext;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.level.CollisionGetter;
import net.minecraft.world.level.pathfinder.PathType;
import net.minecraft.world.level.pathfinder.PathfindingContext;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Every pathfinding search sees Half-Life's walls in its cells' own types. Applied to what comes out
 * of ServerLevel's path type cache, not cached in it: only Minecraft's block updates clear that cache,
 * and Half-Life's doors and lifts move without them.
 */
@Mixin(PathfindingContext.class)
public abstract class PathfindingContextMixin implements HostNavContext {
	@Unique
	private boolean halfcraft$mirror;

	@Inject(method = "<init>", at = @At("RETURN"))
	private void halfcraft$rememberMirror(CollisionGetter level, Mob mob, CallbackInfo ci) {
		this.halfcraft$mirror = HostNav.inMirror(mob.level());
	}

	@Override
	public boolean halfcraft$inMirror() {
		return this.halfcraft$mirror;
	}

	@ModifyReturnValue(method = "getPathTypeFromState", at = @At("RETURN"))
	private PathType halfcraft$hostWalls(PathType type, int x, int y, int z) {
		return this.halfcraft$mirror ? HostNav.rawType(type, x, y, z) : type;
	}
}

package dev.halfcraft.mixin;

import net.minecraft.world.level.Level;
import net.minecraft.world.level.PathNavigationRegion;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** Which level a pathfinding search's region of it belongs to (HostNav.inMirror). */
@Mixin(PathNavigationRegion.class)
public interface PathNavigationRegionAccessor {
	@Accessor("level")
	Level halfcraft$level();
}

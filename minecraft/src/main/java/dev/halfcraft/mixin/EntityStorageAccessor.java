package dev.halfcraft.mixin;

import net.minecraft.server.level.ServerLevel;
import net.minecraft.world.level.chunk.storage.EntityStorage;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** The level an entity storage loads things for (EntityLoadSweepMixin). */
@Mixin(EntityStorage.class)
public interface EntityStorageAccessor {
	@Accessor("level")
	ServerLevel halfcraft$level();
}

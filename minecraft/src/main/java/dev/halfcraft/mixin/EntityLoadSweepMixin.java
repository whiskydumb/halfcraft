package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.world.Rollback;
import java.util.List;
import java.util.stream.Stream;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.level.entity.ChunkEntities;
import net.minecraft.world.level.entity.EntityPersistentStorage;
import net.minecraft.world.level.entity.PersistentEntitySectionManager;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;

/**
 * A chunk of a map the playthrough cleared, loading its things for the first time in the playthrough:
 * what the playthrough before left there never comes into the world (Rollback.sweepsOnLoad), and with
 * the chunk's next save it's gone for good.
 */
@Mixin(PersistentEntitySectionManager.class)
public abstract class EntityLoadSweepMixin {
	@Shadow
	@Final
	private EntityPersistentStorage<?> permanentStorage;

	@WrapOperation(
		method = "processPendingLoads",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/world/level/entity/ChunkEntities;getEntities()Ljava/util/stream/Stream;")
	)
	private Stream<?> halfcraft$sweepLeftBehind(ChunkEntities<?> chunk, Operation<Stream<?>> original) {
		Stream<?> things = original.call(chunk);
		if (!(this.permanentStorage instanceof EntityStorageAccessor storage) || !Rollback.sweepsOnLoad(storage.halfcraft$level(), chunk.getPos())) {
			return things;
		}
		List<?> all = things.toList();
		List<?> kept = all.stream().filter(thing -> !(thing instanceof Entity entity) || !Rollback.isLeftBehind(entity)).toList();
		Rollback.sweptOnLoad(all.size() - kept.size());
		return kept.stream();
	}
}

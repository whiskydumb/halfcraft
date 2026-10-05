package dev.halfcraft.mixin;

import net.minecraft.core.Holder;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.damagesource.DamageType;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.Vec3;
import org.jspecify.annotations.Nullable;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

/**
 * A damage source with both a culprit and a position of its own: a Half-Life grenade is blamed on the
 * soldier who threw it, while a shield has to face where it went off. Vanilla keeps that constructor
 * private.
 */
@Mixin(DamageSource.class)
public interface DamageSourceInvoker {
	@Invoker("<init>")
	static DamageSource halfcraft$create(Holder<DamageType> type, @Nullable Entity directEntity, @Nullable Entity causingEntity, @Nullable Vec3 position) {
		throw new AssertionError();
	}
}

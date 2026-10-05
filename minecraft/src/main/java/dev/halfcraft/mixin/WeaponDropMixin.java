package dev.halfcraft.mixin;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.weapon.HostWeapons;
import net.minecraft.util.Prediction;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Half-Life's weapons never lie on the ground: dropping a stand-in (Q, out of an inventory screen,
 * death without keepInventory) drops nothing, and the next tick puts it back where it was
 * (HostWeapons).
 */
@Mixin(LivingEntity.class)
public abstract class WeaponDropMixin {
	@Inject(
		method = "drop(Lnet/minecraft/world/item/ItemStack;ZLnet/minecraft/util/Prediction;)Lnet/minecraft/world/entity/item/ItemEntity;",
		at = @At("HEAD"),
		cancellable = true
	)
	private void halfcraft$keepStandIns(ItemStack stack, boolean randomly, Prediction prediction, CallbackInfoReturnable<ItemEntity> cir) {
		if (HostWeapons.isStandIn(stack)) {
			if (!((LivingEntity) (Object) this).level().isClientSide()) {
				HalfCraft.LOG.info("HalfCraft: {} isn't dropped (Half-Life's weapon); it goes back into the inventory", stack.getHoverName().getString());
			}
			cir.setReturnValue(null);
		}
	}
}

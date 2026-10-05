package dev.halfcraft.mixin;

import dev.halfcraft.weapon.HostWeapons;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.inventory.Slot;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Half-Life's weapons stay in the player's main inventory: a stand-in fits no chest, furnace,
 * crafting grid, armour or offhand slot (clicks and shift-clicks alike, on both sides).
 */
@Mixin(Slot.class)
public abstract class WeaponSlotMixin {
	@Inject(method = "mayPlace", at = @At("HEAD"), cancellable = true)
	private void halfcraft$mainInventoryOnly(ItemStack stack, CallbackInfoReturnable<Boolean> cir) {
		Slot self = (Slot) (Object) this;
		if (HostWeapons.isStandIn(stack) && !(self.container instanceof Inventory && self.getContainerSlot() < Inventory.INVENTORY_SIZE)) {
			cir.setReturnValue(false);
		}
	}
}

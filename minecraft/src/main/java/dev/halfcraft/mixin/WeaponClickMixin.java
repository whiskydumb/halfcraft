package dev.halfcraft.mixin;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.weapon.HostWeapons;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.inventory.AbstractContainerMenu;
import net.minecraft.world.inventory.ContainerInput;
import net.minecraft.world.item.ItemStack;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * In an inventory screen, Q over one of Half-Life's weapons doesn't throw it and F doesn't swap it
 * into the offhand: neither could stay (WeaponDropMixin, HostWeapons), and the stand-in was gone
 * from its slot for a tick, long enough for Half-Life to put the weapon in hand away and take it out
 * again. On both sides, so the screen doesn't predict the move either.
 */
@Mixin(AbstractContainerMenu.class)
public abstract class WeaponClickMixin {
	@Inject(method = "clicked", at = @At("HEAD"), cancellable = true)
	private void halfcraft$keepStandIn(int slotId, int button, ContainerInput input, Player player, CallbackInfo ci) {
		AbstractContainerMenu self = (AbstractContainerMenu) (Object) this;
		boolean offhand = input == ContainerInput.SWAP && button == Inventory.SLOT_OFFHAND;
		if (input != ContainerInput.THROW && !offhand || slotId < 0 || slotId >= self.slots.size()) {
			return;
		}
		ItemStack stack = self.getSlot(slotId).getItem();
		if (!HostWeapons.isStandIn(stack)) {
			return;
		}
		if (!player.level().isClientSide()) {
			HalfCraft.LOG.info("HalfCraft: {} stays in its slot (Half-Life's weapon): {}", stack.getHoverName().getString(),
				offhand ? "F doesn't swap it into the offhand" : "Q doesn't throw it");
		}
		ci.cancel();
	}
}

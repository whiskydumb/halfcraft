package dev.halfcraft.client;

import dev.halfcraft.link.WeaponTable;
import dev.halfcraft.weapon.HostWeapons;
import java.util.List;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.item.v1.ItemTooltipCallback;
import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.network.chat.Component;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.TooltipFlag;

/**
 * Half-Life's ammo for its weapons' stand-in items: the counter of the one in hand over the hotbar
 * (the action bar), shown when it comes out and whenever its ammo changes, then fading like any
 * action bar message; and every one's counts in its tooltip. Half-Life's own hud stays hidden.
 */
public final class WeaponHud {
	private static String shown = "";

	private WeaponHud() {
	}

	public static void init() {
		ClientTickEvents.END_CLIENT_TICK.register(WeaponHud::tick);
		ItemTooltipCallback.EVENT.register(WeaponHud::tooltip);
	}

	private static void tooltip(ItemStack stack, Item.TooltipContext context, TooltipFlag flag, List<Component> lines) {
		if (!HostWeapons.isStandIn(stack)) {
			return;
		}
		WeaponTable.Weapon weapon = WeaponTable.current().find(HostWeapons.weaponId(stack));
		if (weapon != null) {
			if (weapon.clip() >= 0) {
				lines.add(Component.literal("Clip: " + weapon.clip() + " / " + weapon.maxClip()).withStyle(ChatFormatting.GRAY));
			}
			if (weapon.ammo() >= 0) {
				lines.add(Component.literal("Ammo: " + weapon.ammo() + " / " + weapon.maxAmmo()).withStyle(ChatFormatting.GRAY));
			}
			if (weapon.ammo2() >= 0) {
				lines.add(Component.literal("Alt-fire: " + weapon.ammo2() + " / " + weapon.maxAmmo2()).withStyle(ChatFormatting.GRAY));
			}
		}
		lines.add(Component.literal("Half-Life's weapon: it can't leave the inventory").withStyle(ChatFormatting.DARK_GRAY));
	}

	private static void tick(Minecraft minecraft) {
		if (minecraft.player == null || !HostClient.linked()) {
			shown = "";
			return;
		}
		int id = HostWeapons.weaponId(minecraft.player.getMainHandItem());
		WeaponTable.Weapon weapon = WeaponTable.current().find(id);
		String line = weapon != null ? weapon.ammoLine() : "";
		String key = id + ":" + line;
		if (key.equals(shown)) {
			return;
		}
		shown = key;
		if (!line.isEmpty()) {
			minecraft.player.sendOverlayMessage(Component.literal(line));
		}
	}
}

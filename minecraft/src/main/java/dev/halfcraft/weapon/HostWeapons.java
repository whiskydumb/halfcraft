package dev.halfcraft.weapon;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.Proto;
import dev.halfcraft.link.WeaponTable;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.network.protocol.game.ClientboundSetHeldSlotPacket;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.entity.player.Inventory;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;

/**
 * Half-Life's weapons in Minecraft's inventory, server side (#12).
 *
 * <p>Half-Life's inventory is the authority: every tick the player's inventory is brought in line
 * with Half-Life's WeaponTable ({@link WeaponSync}), one stand-in per weapon, hotbar first. A
 * stand-in can't leave the inventory: Q and F leave it in the hand (WeaponInputMixin), dropping it
 * any other way (WeaponDropMixin) and putting it into a container (WeaponSlotMixin,
 * {@link WeaponItem#canFitInsideContainerItems()}) fail, and whatever slips past (the creative
 * inventory) is put right on the next tick. When Half-Life switches weapons by itself, the hand
 * follows ({@link #followHalfLife}).
 */
public final class HostWeapons {
	// the main inventory, the armour and the offhand
	private static final int SLOTS = WeaponSync.OFFHAND + 1;
	private static final Map<Integer, WeaponItem> ITEMS = new HashMap<>();
	// per player: where each stand-in was last seen, and the weapons waiting for a free slot
	private static final Map<UUID, Map<Integer, Integer>> LAST_SLOTS = new HashMap<>();
	private static final Map<UUID, Set<Integer>> WAITING = new HashMap<>();
	// per player: the weapon Half-Life had out last tick
	private static final Map<UUID, Integer> LAST_OUT = new HashMap<>();

	private HostWeapons() {
	}

	public static void init() {
		for (HostWeapon weapon : HostWeapon.values()) {
			ResourceKey<Item> key = ResourceKey.create(Registries.ITEM, Identifier.fromNamespaceAndPath(HalfCraft.MOD_ID, weapon.path()));
			ITEMS.put(weapon.id(), Registry.register(BuiltInRegistries.ITEM, key, new WeaponItem(new Item.Properties().setId(key).stacksTo(1), weapon)));
		}
		ServerTickEvents.END_SERVER_TICK.register(HostWeapons::serverTick);
	}

	public static boolean isStandIn(ItemStack stack) {
		return stack.getItem() instanceof WeaponItem;
	}

	/** The WeaponId of the stand-in in {@code stack}; 0 for anything else. */
	public static int weaponId(ItemStack stack) {
		return stack.getItem() instanceof WeaponItem item ? item.weapon().id() : Proto.HOST_WEAPON_NONE;
	}

	private static void serverTick(MinecraftServer server) {
		WeaponTable table = WeaponTable.current();
		if (!table.live()) {
			return;  // no Half-Life player right now (a menu, a loading screen): the items stay as they are
		}
		int[] owned = table.weapons().stream().mapToInt(WeaponTable.Weapon::id).filter(ITEMS::containsKey).toArray();
		for (ServerPlayer player : server.getPlayerList().getPlayers()) {
			if (player.isAlive() && !player.isSpectator()) {
				int held = weaponId(player.getInventory().getSelectedItem());
				sync(player, owned);
				followHalfLife(player, table, held);
			}
		}
	}

	/**
	 * Half-Life switched from the weapon the hand held to another by itself, as Half-Life does when
	 * the one out runs dry (the last grenade thrown) or goes away: the hand moves to the new one if
	 * it's on the hotbar. Half-Life keeps it out a moment for that, and puts it away when the hand
	 * stays (hc_weapons.cpp).
	 *
	 * @param held the weapon in the hand before this tick's sync took a gone one out of the inventory
	 */
	private static void followHalfLife(ServerPlayer player, WeaponTable table, int held) {
		int out = table.active();
		Integer before = LAST_OUT.put(player.getUUID(), out);
		if (before == null || before == Proto.HOST_WEAPON_NONE || out == Proto.HOST_WEAPON_NONE || out == before || held != before) {
			return;
		}
		// the hand moved on by itself, and Half-Life followed it before this server heard of it
		if (!goneOrDry(table, before)) {
			return;
		}
		Inventory inventory = player.getInventory();
		for (int slot = 0; slot < Inventory.getSelectionSize(); slot++) {
			if (weaponId(inventory.getItem(slot)) == out) {
				inventory.setSelectedSlot(slot);
				player.connection.send(new ClientboundSetHeldSlotPacket(slot));
				HalfCraft.LOG.info("HalfCraft: Half-Life switched from its {} to its {} by itself: the hand follows (slot {})", name(before), name(out), slot);
				return;
			}
		}
		HalfCraft.LOG.info("HalfCraft: Half-Life switched from its {} to its {} by itself, which isn't on the hotbar: the hand stays", name(before), name(out));
	}

	private static void sync(ServerPlayer player, int[] owned) {
		Inventory inventory = player.getInventory();
		int[] slots = new int[SLOTS];
		for (int slot = 0; slot < SLOTS; slot++) {
			ItemStack stack = inventory.getItem(slot);
			slots[slot] = stack.isEmpty() ? WeaponSync.EMPTY : weaponId(stack);
		}
		int carried = weaponId(player.containerMenu.getCarried());
		Map<Integer, Integer> lastSlots = LAST_SLOTS.computeIfAbsent(player.getUUID(), k -> new HashMap<>());
		List<WeaponSync.Change> changes = WeaponSync.plan(slots, owned, carried, lastSlots);
		for (WeaponSync.Change change : changes) {
			apply(player, change);
		}

		Set<Integer> present = new HashSet<>();
		for (int slot = 0; slot < WeaponSync.MAIN_SLOTS; slot++) {
			int id = weaponId(inventory.getItem(slot));
			if (id != Proto.HOST_WEAPON_NONE) {
				lastSlots.put(id, slot);
				present.add(id);
			}
		}
		present.add(weaponId(player.containerMenu.getCarried()));
		Set<Integer> waiting = WAITING.computeIfAbsent(player.getUUID(), k -> new HashSet<>());
		for (int id : owned) {
			if (present.contains(id)) {
				waiting.remove(id);
			} else if (waiting.add(id)) {
				HalfCraft.LOG.info("HalfCraft: no free slot for Half-Life's {}; it comes once there is one", name(id));
			}
		}
	}

	private static void apply(ServerPlayer player, WeaponSync.Change change) {
		Inventory inventory = player.getInventory();
		switch (change) {
			case WeaponSync.Add add -> {
				inventory.setItem(add.slot(), new ItemStack(ITEMS.get(add.weapon())));
				HalfCraft.LOG.info("HalfCraft: Half-Life's {} is in the inventory now (slot {})", name(add.weapon()), add.slot());
			}
			case WeaponSync.Remove remove -> {
				int id = weaponId(inventory.getItem(remove.slot()));
				inventory.setItem(remove.slot(), ItemStack.EMPTY);
				HalfCraft.LOG.info("HalfCraft: took Half-Life's {} out of slot {} (Half-Life doesn't have it, or it was there twice)", name(id), remove.slot());
			}
			case WeaponSync.Swap swap -> {
				ItemStack from = inventory.getItem(swap.from());
				inventory.setItem(swap.from(), inventory.getItem(swap.to()));
				inventory.setItem(swap.to(), from);
				HalfCraft.LOG.info("HalfCraft: Half-Life's {} went back from slot {} to slot {}", name(weaponId(from)), swap.from(), swap.to());
			}
			case WeaponSync.ClearCarried _ -> {
				int id = weaponId(player.containerMenu.getCarried());
				player.containerMenu.setCarried(ItemStack.EMPTY);
				HalfCraft.LOG.info("HalfCraft: took Half-Life's {} off the cursor (Half-Life doesn't have it, or it was there twice)", name(id));
			}
		}
	}

	/** Half-Life no longer has the weapon, or it has no rounds left at all (one without ammo never runs dry). */
	private static boolean goneOrDry(WeaponTable table, int id) {
		WeaponTable.Weapon weapon = table.find(id);
		if (weapon == null) {
			return true;
		}
		return (weapon.clip() >= 0 || weapon.ammo() >= 0) && weapon.clip() <= 0 && weapon.ammo() <= 0;
	}

	private static String name(int id) {
		HostWeapon weapon = HostWeapon.byId(id);
		return weapon != null ? weapon.path() : "weapon " + id;
	}

	/** Takes the stand-ins out of an inventory (Rollback keeps Half-Life's weapons out of its checkpoints): slot to item. */
	public static Map<Integer, ItemStack> takeOut(Inventory inventory) {
		Map<Integer, ItemStack> taken = new HashMap<>();
		for (int slot = 0; slot < SLOTS; slot++) {
			ItemStack stack = inventory.getItem(slot);
			if (isStandIn(stack)) {
				taken.put(slot, stack);
				inventory.setItem(slot, ItemStack.EMPTY);
			}
		}
		return taken;
	}

	/** Puts stand-ins taken out back, where they were if that's free, else anywhere free; the next tick puts the rest right. */
	public static void putBack(Inventory inventory, Map<Integer, ItemStack> standIns) {
		standIns.forEach((slot, stack) -> {
			if (inventory.getItem(slot).isEmpty()) {
				inventory.setItem(slot, stack);
			} else {
				inventory.add(stack);
			}
		});
	}
}

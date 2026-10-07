package dev.halfcraft.weapon;

import dev.halfcraft.link.WeaponTable;
import net.minecraft.core.BlockPos;
import net.minecraft.network.chat.Component;
import net.minecraft.util.Mth;
import net.minecraft.world.InteractionHand;
import net.minecraft.world.InteractionResult;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.item.Item;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.context.UseOnContext;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.state.BlockState;
import org.jspecify.annotations.Nullable;

/**
 * The hotbar item that stands in for one of Half-Life's weapons. Holding it takes the weapon out in
 * Half-Life, whose clicks fire it; in Minecraft it does nothing at all. Its ammo shows as the item's
 * bar, read live from Half-Life (and in its tooltip and over the hotbar: WeaponHud).
 */
public final class WeaponItem extends Item {
	private final HostWeapon weapon;

	public WeaponItem(Properties properties, HostWeapon weapon) {
		super(properties);
		this.weapon = weapon;
	}

	public HostWeapon weapon() {
		return this.weapon;
	}

	private WeaponTable.@Nullable Weapon live() {
		return WeaponTable.current().find(this.weapon.id());
	}

	@Override
	public Component getName(ItemStack stack) {
		WeaponTable.Weapon live = this.live();
		Component name = super.getName(stack);
		return live != null && live.supercharged() ? Component.translatable("item.halfcraft.supercharged", name) : name;
	}

	@Override
	public boolean isBarVisible(ItemStack stack) {
		WeaponTable.Weapon live = this.live();
		return live != null && live.fill() >= 0.0F;
	}

	@Override
	public int getBarWidth(ItemStack stack) {
		WeaponTable.Weapon live = this.live();
		return live != null ? Math.round(MAX_BAR_WIDTH * Math.max(0.0F, live.fill())) : 0;
	}

	@Override
	public int getBarColor(ItemStack stack) {
		WeaponTable.Weapon live = this.live();
		return Mth.hsvToRgb(live != null ? Math.max(0.0F, live.fill()) / 3.0F : 0.0F, 1.0F, 1.0F);
	}

	/** Not into bundles or shulker boxes (other containers refuse it in WeaponSlotMixin). */
	@Override
	public boolean canFitInsideContainerItems() {
		return false;
	}

	@Override
	public boolean canDestroyBlock(ItemStack stack, BlockState state, Level level, BlockPos pos, LivingEntity entity) {
		return false;
	}

	@Override
	public InteractionResult use(Level level, Player player, InteractionHand hand) {
		return InteractionResult.FAIL;
	}

	@Override
	public InteractionResult useOn(UseOnContext context) {
		return InteractionResult.FAIL;
	}
}

package dev.halfcraft.client.mixin;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.weapon.HostWeapons;
import net.minecraft.client.KeyMapping;
import net.minecraft.client.Minecraft;
import org.objectweb.asm.Opcodes;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * With Half-Life's weapon in hand, Minecraft's own controls leave it alone. Attack and use do
 * nothing: Half-Life gets those clicks (hc_input), and one that still arrives (a rebound key, a
 * test harness) must not break, hit or place anything with a gun, nor use what the offhand holds.
 * Q and F don't take it out of the hand either: a drop predicted here, or a swap the server sends
 * back a tick later, would empty the hand long enough for Half-Life to put the weapon away and
 * take it out again (the server's HostWeapons puts back whatever still slips past).
 */
@Mixin(Minecraft.class)
public abstract class WeaponInputMixin {
	// the key whose presses are being swallowed, said once until it's let go
	@Unique
	private KeyMapping halfcraft$swallowing;

	private boolean halfcraft$holdsStandIn() {
		Minecraft self = (Minecraft) (Object) this;
		return self.player != null && HostWeapons.isStandIn(self.player.getMainHandItem());
	}

	@Inject(method = "startAttack", at = @At("HEAD"), cancellable = true)
	private void halfcraft$noAttack(CallbackInfoReturnable<Boolean> cir) {
		if (this.halfcraft$holdsStandIn()) {
			cir.setReturnValue(false);
		}
	}

	@Inject(method = "continueAttack", at = @At("HEAD"), cancellable = true)
	private void halfcraft$noMining(boolean leftClick, CallbackInfo ci) {
		Minecraft self = (Minecraft) (Object) this;
		if (this.halfcraft$holdsStandIn()) {
			if (self.gameMode != null) {
				self.gameMode.stopDestroyBlock();
			}
			ci.cancel();
		}
	}

	@Inject(method = "startUseItem", at = @At("HEAD"), cancellable = true)
	private void halfcraft$noUse(CallbackInfo ci) {
		if (this.halfcraft$holdsStandIn()) {
			ci.cancel();
		}
	}

	// after the hotbar keys of the same tick, so a weapon picked with 1-9 counts as in hand
	@Inject(
		method = "handleKeybinds",
		at = @At(value = "FIELD", target = "Lnet/minecraft/client/Options;keySwapOffhand:Lnet/minecraft/client/KeyMapping;", opcode = Opcodes.GETFIELD)
	)
	private void halfcraft$noSwap(CallbackInfo ci) {
		this.halfcraft$swallow(((Minecraft) (Object) this).options.keySwapOffhand, "F doesn't swap it into the offhand");
	}

	@Inject(
		method = "handleKeybinds",
		at = @At(value = "FIELD", target = "Lnet/minecraft/client/Options;keyDrop:Lnet/minecraft/client/KeyMapping;", opcode = Opcodes.GETFIELD)
	)
	private void halfcraft$noDrop(CallbackInfo ci) {
		this.halfcraft$swallow(((Minecraft) (Object) this).options.keyDrop, "Q doesn't drop it");
	}

	/** Eats the key's pending presses while a stand-in is in hand; a held key repeats, and is said once. */
	private void halfcraft$swallow(KeyMapping key, String what) {
		Minecraft self = (Minecraft) (Object) this;
		boolean pressed = false;
		if (this.halfcraft$holdsStandIn()) {
			while (key.consumeClick()) {
				pressed = true;
			}
		}
		if (pressed && this.halfcraft$swallowing != key) {
			this.halfcraft$swallowing = key;
			HalfCraft.LOG.info("HalfCraft: {} stays in hand (Half-Life's weapon): {}", self.player.getMainHandItem().getHoverName().getString(), what);
		} else if (!pressed && this.halfcraft$swallowing == key && !key.isDown()) {
			this.halfcraft$swallowing = null;
		}
	}
}

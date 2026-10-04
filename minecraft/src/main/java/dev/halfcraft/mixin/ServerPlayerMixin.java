package dev.halfcraft.mixin;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.combat.HostCombat;
import dev.halfcraft.combat.HostActorEntity;
import dev.halfcraft.link.Proto;
import dev.halfcraft.link.HostLink;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.server.level.ServerPlayer;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

@Mixin(ServerPlayer.class)
public abstract class ServerPlayerMixin {
	/** Critical hits on a Half-Life actor are flagged so Half-Life can play them up. */
	@Inject(method = "crit", at = @At("HEAD"))
	private void halfcraft$critHost(Entity entity, CallbackInfo ci) {
		if (entity instanceof HostActorEntity proxy) {
			proxy.markCritical();
		}
	}

	/** Dying in Minecraft is dying in Half-Life. */
	@Inject(method = "die", at = @At("HEAD"))
	private void halfcraft$diesInHost(DamageSource source, CallbackInfo ci) {
		ServerPlayer self = (ServerPlayer) (Object) this;
		int attacker = HostCombat.attackerActorId(source);
		if (HostLink.active()) {
			HostLink.pushEvent(Proto.EV_PLAYER_DIED, attacker, 0, 0, 0, 0, 0);
			HalfCraft.LOG.info("HalfCraft: player died ({}); telling Half-Life", source.getMsgId());
		}
	}
}

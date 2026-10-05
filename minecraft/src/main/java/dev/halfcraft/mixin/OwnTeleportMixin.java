package dev.halfcraft.mixin;

import dev.halfcraft.link.HostTeleports;
import dev.halfcraft.mobs.HostNav;
import java.util.Set;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.network.ServerGamePacketListenerImpl;
import net.minecraft.world.entity.PositionMoveRotation;
import net.minecraft.world.entity.Relative;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Every teleport of a player (an ender pearl, chorus fruit, /tp, a respawn, the server putting the
 * player back) goes out as one position packet. In the mirror world the ones Half-Life didn't ask for
 * are marked, right before the packet leaves, so the client can count them for Half-Life
 * ({@link HostTeleports}).
 */
@Mixin(ServerGamePacketListenerImpl.class)
public abstract class OwnTeleportMixin {
	@Shadow
	public ServerPlayer player;

	@Shadow
	private int awaitingTeleport;

	@Inject(
		method = "teleport(Lnet/minecraft/world/entity/PositionMoveRotation;Ljava/util/Set;)V",
		at = @At(value = "INVOKE", target = "Lnet/minecraft/server/network/ServerGamePacketListenerImpl;send(Lnet/minecraft/network/protocol/Packet;)V")
	)
	private void halfcraft$markOwnTeleport(PositionMoveRotation change, Set<Relative> relatives, CallbackInfo ci) {
		if (HostNav.inMirror(this.player.level())) {
			HostTeleports.serverSent(this.awaitingTeleport);
		}
	}
}

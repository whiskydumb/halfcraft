package dev.halfcraft.client.mixin;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.HostTeleports;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientPacketListener;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.network.protocol.game.ClientboundPlayerPositionPacket;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * The server moved the player: when it was one of Minecraft's own teleports (the integrated server
 * marked it, {@link HostTeleports}), it counts for Half-Life from this tick on, together with the new
 * position.
 */
@Mixin(ClientPacketListener.class)
public abstract class PlayerPositionMixin {
	@Inject(method = "handleMovePlayer", at = @At("TAIL"))
	private void halfcraft$countOwnTeleport(ClientboundPlayerPositionPacket packet, CallbackInfo ci) {
		LocalPlayer player = Minecraft.getInstance().player;
		if (player != null && HostTeleports.clientApplied(packet.id(), !player.isPassenger())) {
			HalfCraft.LOG.info("HalfCraft: Minecraft moved its player by itself (teleport {}) to {} {} {}", HostTeleports.count(),
				String.format("%.2f", player.getX()), String.format("%.2f", player.getY()), String.format("%.2f", player.getZ()));
		}
	}
}

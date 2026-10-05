package dev.halfcraft.client;

import dev.halfcraft.combat.HostCombat;
import net.fabricmc.api.ClientModInitializer;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.fabricmc.fabric.api.client.rendering.v1.EntityRendererRegistry;
import net.minecraft.client.renderer.entity.NoopRenderer;

public final class HalfCraftClient implements ClientModInitializer {
	@Override
	public void onInitializeClient() {
		dev.halfcraft.link.HostLink.announceRunning();
		HostCommands.register();

		ClientTickEvents.END_CLIENT_TICK.register(HostClient::clientTick);
		HostScreenshots.init();
		// Half-Life draws the real NPC; its Minecraft stand-in is only a hitbox.
		EntityRendererRegistry.register(HostCombat.HOST_ACTOR, NoopRenderer::new);
		// Players (client-side movement AND the integrated server's re-check of it) use the smooth
		// triangle collider, never Half-Life's voxels; otherwise the server sees the smooth position
		// dip into a voxel and teleports the player back every few ticks.
		dev.halfcraft.world.HostCollision.setSmoothCollider(e -> e instanceof net.minecraft.world.entity.player.Player && HostClient.linked());
	}
}

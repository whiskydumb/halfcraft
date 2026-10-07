package dev.halfcraft.client;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.HostLink;

/**
 * How fast Half-Life lets its player move (its player_speedmod, HostState's speedFactor), for the
 * local player's walking input (PlayerSpeedModMixin). Render thread.
 */
public final class HostSpeed {
	// a factor this close to 1 is none (it comes as a float over the link)
	private static final float NONE_EPS = 1.0E-3F;
	private static float logged = 1.0F;

	private HostSpeed() {
	}

	/** The factor for Minecraft's player's walking: 1 unless Half-Life slows (or speeds) its own. */
	public static float factor() {
		HostLink.HostState sky = HostClient.sky();
		float factor = HostClient.linked() && sky.inGame() && !sky.loading() ? sky.speedFactor : 1.0F;
		// an older Half-Life leaves the field at 0, and a bad value is no reason to stop the player
		if (!(factor > 0.0F) || factor > 4.0F || Math.abs(factor - 1.0F) < NONE_EPS) {
			factor = 1.0F;
		}
		if (factor != logged) {
			logged = factor;
			HalfCraft.LOG.info("HalfCraft: Half-Life sets the player's speed to {}%", Math.round(factor * 100.0F));
		}
		return factor;
	}
}

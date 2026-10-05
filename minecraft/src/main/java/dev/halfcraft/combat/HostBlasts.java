package dev.halfcraft.combat;

import net.minecraft.world.entity.Mob;
import net.minecraft.world.level.Explosion;

/**
 * Minecraft's explosions as Half-Life gets them (Proto.EV_EXPLOSION): Half-Life repeats each one with
 * its own blast, which alone hurts its characters and breaks its props (see hc_blast.h).
 */
public final class HostBlasts {
	private HostBlasts() {
	}

	/** A wind charge's or a breeze's burst: it only pushes and triggers blocks, so Half-Life has no blast for it. */
	public static boolean isWindBurst(Explosion explosion) {
		return explosion.getBlockInteraction() == Explosion.BlockInteraction.TRIGGER_BLOCK;
	}

	/**
	 * The mob that set the explosion off (a creeper, a ghast's fireball): Half-Life blames the blast on
	 * its stand-in. 0 for nobody: TNT, even when the player lit it, is nobody's.
	 */
	public static int culpritId(Explosion explosion) {
		return explosion.getIndirectSourceEntity() instanceof Mob mob ? mob.getId() : 0;
	}
}

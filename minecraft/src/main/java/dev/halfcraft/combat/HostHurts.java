package dev.halfcraft.combat;

import dev.halfcraft.link.Proto;

/**
 * How Minecraft takes each kind of Half-Life hit (a Proto.HURT_* value): which damage type, and what it
 * comes from. Pure, so it can be tested; {@link HostCombat#damageSource} builds the source from it.
 *
 * <p>The damage type decides what protects the player. Blasts are explosions to Minecraft: armour and
 * blast protection reduce them, and a raised shield facing the blast blocks them. Fire is reduced by
 * armour and fire protection, and every tick of Half-Life's burning counts: Minecraft's own fire skips
 * hits that come within its hurt cooldown, which let one in three through. Crushes are reduced by
 * armour (a shield can't hold off a falling prop). HalfCraft's
 * own types scale with Minecraft's difficulty only when a Half-Life character dealt them, like the
 * stand-ins' hits, so a barrel or a map's blast hurts the same on every difficulty. Melee and bullets
 * from a character with a stand-in come from that stand-in, so the shield faces it and pets defend the
 * player. From one Minecraft doesn't know (out of range, a mounted gun) they take HalfCraft's own
 * types, which armour reduces and a shield facing where the hit came from blocks. Anything else stays
 * generic, which nothing protects from.
 *
 * <p>HalfCraft's own types skip Minecraft's default knockback (they're in #no_knockback): a hit with no
 * position would shove the target in a random direction. {@link HostCombat#knockBack} pushes the target
 * away from the position instead, when there is one.
 */
public final class HostHurts {
	private HostHurts() {
	}

	/**
	 * What a hit becomes.
	 *
	 * @param typeId the damage type
	 * @param standInDirect the attacker's stand-in dealt it itself (the shield faces the stand-in)
	 * @param standInCausing the attacker's stand-in gets the blame (pets defend the player against it)
	 * @param positioned it comes from where Half-Life said ({@code IN_HURT_FROM}): the shield faces that
	 */
	public record Recipe(String typeId, boolean standInDirect, boolean standInCausing, boolean positioned) {
	}

	public static final String BLAST = "halfcraft:host_blast";
	public static final String MELEE = "halfcraft:host_melee";
	public static final String BULLET = "halfcraft:host_bullet";
	public static final String CRUSH = "halfcraft:host_crush";
	public static final String BURN = "halfcraft:host_burn";

	/**
	 * @param kind a Proto.HURT_* value
	 * @param hasStandIn the attacker is a Half-Life character Minecraft has a stand-in for
	 */
	public static Recipe recipe(int kind, boolean hasStandIn) {
		return switch (kind) {
			case Proto.HURT_MELEE -> hasStandIn ? new Recipe("minecraft:mob_attack", true, true, false) : new Recipe(MELEE, false, false, true);
			case Proto.HURT_PROJECTILE -> hasStandIn ? new Recipe("minecraft:mob_projectile", true, true, false) : new Recipe(BULLET, false, false, true);
			case Proto.HURT_MAGIC -> hasStandIn ? new Recipe("minecraft:indirect_magic", true, true, false) : new Recipe("minecraft:magic", false, false, false);
			case Proto.HURT_BLAST -> new Recipe(BLAST, false, hasStandIn, true);
			case Proto.HURT_FIRE -> new Recipe(BURN, false, hasStandIn, true);
			case Proto.HURT_CRUSH -> new Recipe(CRUSH, false, hasStandIn, true);
			default -> new Recipe("minecraft:generic", false, false, false);
		};
	}

	/** Whether this damage type is one of HalfCraft's own (see the class comment about knockback). */
	public static boolean isOwnType(String typeId) {
		return typeId.startsWith("halfcraft:");
	}

	/** Whether a hit of this type pushes the target away from where it came from: HalfCraft's own types but burns. */
	public static boolean knocksBack(String typeId) {
		return isOwnType(typeId) && !BURN.equals(typeId);
	}

	/** A coordinate of {@code IN_HURT_FROM}: Half-Life sends floats as their bits. */
	public static double coordinate(int bits) {
		return Float.intBitsToFloat(bits);
	}
}

package dev.halfcraft;

import java.util.regex.Pattern;

/**
 * The Minecraft world Half-Life plays in: "HalfCraft", or another one named with -Dhalfcraft.world.
 * The test client (make mc-test) plays in "HalfCraftTest", so tests never touch the player's world.
 */
public final class WorldName {
	public static final String DEFAULT = "HalfCraft";
	// a plain folder under saves/: nothing that could reach outside it
	private static final Pattern VALID = Pattern.compile("[A-Za-z0-9 _-]{1,64}");

	private WorldName() {
	}

	/** The world {@code property} (a -Dhalfcraft.world value, or null) names; the default one when it names none we take. */
	public static String resolve(String property) {
		if (property == null || property.isBlank()) {
			return DEFAULT;
		}
		String name = property.strip();
		if (!VALID.matcher(name).matches()) {
			HalfCraft.LOG.warn("HalfCraft: -Dhalfcraft.world={} isn't a plain world name; playing in {}", property, DEFAULT);
			return DEFAULT;
		}
		if (!name.equals(DEFAULT)) {
			HalfCraft.LOG.info("HalfCraft: playing in world {} (-Dhalfcraft.world)", name);
		}
		return name;
	}
}

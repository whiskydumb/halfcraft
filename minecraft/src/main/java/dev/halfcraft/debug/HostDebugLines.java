package dev.halfcraft.debug;

import dev.halfcraft.link.HostDebug;
import dev.halfcraft.link.Proto;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * The debug screen's (F3) lines about Half-Life, from what it reported ({@link HostDebug}). Half-Life
 * positions are its own units (40 to a block, z up); "mirror" is where Minecraft's player stands in
 * the map's slot of Minecraft's world.
 */
public final class HostDebugLines {
	private static final String[] RELATIONS = { "", "hates you", "fears you", "likes you", "neutral" };
	private static final String[] NPC_STATES = { "", "idle", "alert", "combat", "scripted", "playing dead", "held by a barnacle", "dead" };
	private static final double UNITS_PER_BLOCK = 40.0;

	private HostDebugLines() {
	}

	/** Frame rate, map, chapter and where Half-Life's player is and looks. */
	public static List<String> location(HostDebug d, double mirrorX, double mirrorY, double mirrorZ) {
		List<String> lines = new ArrayList<>();
		lines.add(format("Half-Life: %.0f fps (worst frame %.1f ms), overlay %.2f ms", d.fps, d.worstFrameMs, d.overlayMs));
		if (d.map.isEmpty()) {
			lines.add("Map: none loaded");
			return lines;
		}
		lines.add("Map: " + d.map + chapter(d));
		lines.add(format("Source XYZ: %.2f / %.2f / %.2f", d.origin[0], d.origin[1], d.origin[2]));
		String roll = d.angles[2] != 0.0F ? format(", roll %.1f", d.angles[2]) : "";
		lines.add(format("Source facing: yaw %.1f, pitch %.1f%s", d.angles[1], d.angles[0], roll));
		lines.add(format("Mirror XYZ: %.3f / %.3f / %.3f (map slot %d, block grid at Source z %.0f)", mirrorX, mirrorY, mirrorZ, d.slot, d.gridZ));
		return lines;
	}

	/** Who drives the player, the rings between the games, Minecraft's lights in Half-Life, edicts. */
	public static List<String> link(HostDebug d) {
		List<String> lines = new ArrayList<>();
		String driver = d.puppet() ? "Minecraft drives the player" : "Half-Life holds the player";
		String input = d.minecraftInput() ? "Minecraft has the input" : "Half-Life has the input";
		lines.add(format("Link: collision epoch %d, %s, %s", d.collisionEpoch, driver, input));
		lines.add(format("Rings: input %s, events %s, collision %s, render %s", entries(d.inputPending), entries(d.eventPending), bytes(d.collisionPending),
			bytes(d.renderPending)));
		lines.add(format("Block lights: %d emitters, %d lights, %d shadowed", d.lightEmitters, d.lights, d.shadowedLights));
		if (d.serverCurrent()) {
			lines.add(format("Edicts: %d", d.entityCount));
		}
		return lines;
	}

	/** What's under Half-Life's crosshair. */
	public static List<String> target(HostDebug d) {
		List<String> lines = new ArrayList<>();
		if (!d.hasTarget()) {
			lines.add("Half-Life target: nothing");
			return lines;
		}
		String name = d.targetName.isEmpty() ? "" : " \"" + d.targetName + "\"";
		lines.add(format("Half-Life target: %s #%d%s", d.targetClass, d.targetIndex, name));
		StringBuilder state = new StringBuilder(format("Health: %d / %d", d.health, d.maxHealth));
		appendName(state, RELATIONS, d.relation);
		appendName(state, NPC_STATES, d.npcState);
		lines.add(state.toString());
		if (!d.schedule.isEmpty()) {
			lines.add("Schedule: " + d.schedule);
		}
		lines.add(format("Distance: %.0f units (%.1f blocks)", d.distance, d.distance / UNITS_PER_BLOCK));
		return lines;
	}

	private static String chapter(HostDebug d) {
		if (d.chapter.isEmpty()) {
			return "";
		}
		return ", chapter " + d.chapter + (d.chapterTitle.isEmpty() ? "" : ": " + d.chapterTitle);
	}

	private static void appendName(StringBuilder line, String[] names, int index) {
		if (index > 0 && index < names.length) {
			line.append(", ").append(names[index]);
		}
	}

	/** A ring's pending entries; all ones (-1 here) while its indices disagree (Proto.RING_OUT_OF_STEP). */
	private static String entries(int count) {
		return count == (int) Proto.RING_OUT_OF_STEP ? "out of step" : Integer.toString(count);
	}

	private static String bytes(long count) {
		if (count == Proto.RING_OUT_OF_STEP) {
			return "out of step";
		}
		if (count < 1024) {
			return count + " B";
		}
		if (count < 1024 * 1024) {
			return format("%.1f KB", count / 1024.0);
		}
		return format("%.1f MB", count / (1024.0 * 1024.0));
	}

	private static String format(String pattern, Object... args) {
		return String.format(Locale.ROOT, pattern, args);
	}
}

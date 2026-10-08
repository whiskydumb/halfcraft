package dev.halfcraft.client.debug;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.client.FrameExporter;
import dev.halfcraft.client.HostClient;
import dev.halfcraft.debug.HostDebugLines;
import dev.halfcraft.link.HostDebug;
import dev.halfcraft.link.HostLink;
import java.util.ArrayList;
import java.util.Collections;
import java.util.IdentityHashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.Function;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.components.debug.DebugScreenEntries;
import net.minecraft.client.gui.components.debug.DebugScreenEntry;
import net.minecraft.resources.Identifier;
import net.minecraft.world.entity.Entity;

/**
 * Half-Life's side of Minecraft's debug screen (F3). While Half-Life is linked, its map and chapter,
 * Source coordinates and angles, frame rate, link state and what's under its crosshair show as
 * debug entries of their own (on in the overlay by default, toggled like any other in the debug
 * options), and Minecraft's entries about the mirror world's terrain, whose coordinates are shifted
 * by the map's slot and mean nothing in Half-Life, are left out.
 */
public final class HostDebugScreen {
	/** Our entries; HostDebugEntriesMixin registers them, HostDebugEntryListMixin turns them on. */
	public static final Map<Identifier, DebugScreenEntry> ENTRIES = new LinkedHashMap<>();
	private static final long READ_EVERY_NS = 50_000_000L;

	private static final HostDebug DEBUG = new HostDebug();
	private static Set<DebugScreenEntry> hidden;
	private static long readAt;
	private static int readGeneration = -1;
	private static boolean haveDebug;
	private static String shown = "";

	static {
		entry("host", d -> {
			Entity camera = Minecraft.getInstance().getCameraEntity();
			List<String> lines = new ArrayList<>(camera == null ? HostDebugLines.location(d, 0, 0, 0) : HostDebugLines.location(d, camera.getX(), camera.getY(), camera.getZ()));
			lines.add(1, HostDebugLines.overlay(FrameExporter.width(), FrameExporter.height(), HostClient.overlayDivisor(), FrameExporter.readbackMs()));
			return lines;
		});
		entry("host_link", HostDebugLines::link);
		entry("host_target", HostDebugLines::target);
	}

	private HostDebugScreen() {
	}

	private static void entry(String path, Function<HostDebug, List<String>> lines) {
		Identifier id = Identifier.fromNamespaceAndPath(HalfCraft.MOD_ID, path);
		ENTRIES.put(id, (displayer, level, clientChunk, serverChunk) -> {
			HostDebug d = current();
			if (d != null) {
				displayer.addToGroup(id, lines.apply(d));
			}
		});
	}

	/** Whether Minecraft's own entry is left out of the debug screen right now (Half-Life is linked). */
	public static boolean hides(DebugScreenEntry entry) {
		if (!HostClient.linked()) {
			return false;
		}
		if (hidden == null) {
			Set<DebugScreenEntry> set = Collections.newSetFromMap(new IdentityHashMap<>());
			for (Identifier id : List.of(DebugScreenEntries.PLAYER_POSITION, DebugScreenEntries.PLAYER_SECTION_POSITION, DebugScreenEntries.BIOME,
				DebugScreenEntries.HEIGHTMAP, DebugScreenEntries.LIGHT_LEVELS, DebugScreenEntries.CHUNK_GENERATION_STATS)) {
				DebugScreenEntry known = DebugScreenEntries.getEntry(id);
				if (known != null) {
					set.add(known);
				}
			}
			hidden = set;
		}
		return hidden.contains(entry);
	}

	/** What Half-Life last reported, read at most every 50 ms; null while unlinked or before its first report. */
	private static HostDebug current() {
		if (!HostClient.linked()) {
			return null;
		}
		long now = System.nanoTime();
		if (now - readAt >= READ_EVERY_NS) {
			readAt = now;
			if (readGeneration != HostLink.generation()) {
				readGeneration = HostLink.generation();
				haveDebug = false; // what another Half-Life reported is gone
			}
			haveDebug = HostDebug.read(HostLink.segment(), DEBUG) || haveDebug;
			if (haveDebug) {
				logShown();
			}
		}
		return haveDebug ? DEBUG : null;
	}

	/** One log line per map the debug screen first shows, to check a screenshot against. */
	private static void logShown() {
		String now = HostLink.generation() + "/" + DEBUG.map;
		if (!now.equals(shown)) {
			shown = now;
			HalfCraft.LOG.info("HalfCraft: debug screen shows Half-Life's {} (chapter {})", DEBUG.map.isEmpty() ? "menu" : DEBUG.map,
				DEBUG.chapter.isEmpty() ? "none" : DEBUG.chapter);
		}
	}
}

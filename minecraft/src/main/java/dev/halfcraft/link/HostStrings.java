package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;

import dev.halfcraft.HalfCraft;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.function.Consumer;

/**
 * Strings Half-Life sends in pieces (IN_STRING): each channel collects its pieces until the last
 * one, then hands the whole string to the handler registered for it. Render thread only.
 */
public final class HostStrings {
	// a string that never ends (a lost last piece) is dropped past this
	private static final int MAX_BYTES = 1 << 16;
	private static final Map<Integer, Consumer<String>> HANDLERS = new HashMap<>();
	private static final Map<Integer, ByteArrayOutputStream> PENDING = new HashMap<>();

	private HostStrings() {
	}

	/** What to do with a whole string on {@code channel} (a StringChannel). */
	public static void register(int channel, Consumer<String> handler) {
		HANDLERS.put(channel, handler);
	}

	/** One IN_STRING entry. */
	public static void accept(int code, int a, int b, int c) {
		int channel = code & STRING_CHANNEL_MASK;
		int bytes = Math.min((code >>> STRING_BYTES_SHIFT) & 0xF, STRING_PIECE_BYTES);
		ByteArrayOutputStream pending = PENDING.computeIfAbsent(channel, k -> new ByteArrayOutputStream());
		int[] words = { a, b, c };
		for (int i = 0; i < bytes; i++) {
			pending.write(words[i / 4] >>> (8 * (i % 4)));
		}
		if (pending.size() > MAX_BYTES) {
			HalfCraft.LOG.warn("HalfCraft: string on channel {} from Half-Life never ended; dropped", channel);
			PENDING.remove(channel);
			return;
		}
		if ((code & STRING_END) == 0) {
			return;
		}
		PENDING.remove(channel);
		String text = pending.toString(StandardCharsets.UTF_8);
		Consumer<String> handler = HANDLERS.get(channel);
		if (handler == null) {
			HalfCraft.LOG.warn("HalfCraft: nothing handles string channel {} from Half-Life", channel);
			return;
		}
		handler.accept(text);
	}

	/** A new Half-Life on the other end: whatever the old one left half-sent goes. */
	public static void reset() {
		PENDING.clear();
	}
}

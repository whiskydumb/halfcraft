package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class HostStringsTest {
	/** The pieces Half-Life's Link::push_string sends for {@code text}, as IN_STRING entries {code, a, b, c}. */
	private static List<int[]> pieces(int channel, String text) {
		byte[] utf8 = text.getBytes(StandardCharsets.UTF_8);
		int count = Math.max(1, (utf8.length + STRING_PIECE_BYTES - 1) / STRING_PIECE_BYTES);
		List<int[]> out = new ArrayList<>();
		for (int piece = 0; piece < count; piece++) {
			int offset = piece * STRING_PIECE_BYTES;
			int bytes = Math.min(STRING_PIECE_BYTES, utf8.length - offset);
			int[] words = new int[3];
			for (int i = 0; i < bytes; i++) {
				words[i / 4] |= (utf8[offset + i] & 0xFF) << (8 * (i % 4));
			}
			int code = channel | bytes << STRING_BYTES_SHIFT | (piece + 1 == count ? STRING_END : 0);
			out.add(new int[] { code, words[0], words[1], words[2] });
		}
		return out;
	}

	private static void send(List<int[]> pieces) {
		for (int[] p : pieces) {
			HostStrings.accept(p[0], p[1], p[2], p[3]);
		}
	}

	@AfterEach
	void reset() {
		HostStrings.reset();
	}

	@Test
	void reassemblesAStringLongerThanOnePiece() {
		List<String> got = new ArrayList<>();
		HostStrings.register(STR_COMMAND, got::add);
		send(pieces(STR_COMMAND, "give @s minecraft:trident 1"));
		assertEquals(List.of("give @s minecraft:trident 1"), got);
	}

	@Test
	void keepsMultiByteCharactersSplitAcrossPieces() {
		List<String> got = new ArrayList<>();
		HostStrings.register(STR_SCREENSHOT, got::add);
		String text = "C:\\Users\\игрок\\screenshots\\кадр.png";
		send(pieces(STR_SCREENSHOT, text));
		assertEquals(List.of(text), got);
	}

	@Test
	void deliversAnEmptyString() {
		List<String> got = new ArrayList<>();
		HostStrings.register(STR_COMMAND, got::add);
		send(pieces(STR_COMMAND, ""));
		assertEquals(List.of(""), got);
	}

	@Test
	void keepsChannelsApart() {
		List<String> commands = new ArrayList<>();
		List<String> shots = new ArrayList<>();
		HostStrings.register(STR_COMMAND, commands::add);
		HostStrings.register(STR_SCREENSHOT, shots::add);
		List<int[]> a = pieces(STR_COMMAND, "time set midnight please");
		List<int[]> b = pieces(STR_SCREENSHOT, "a second, longer string on another channel");
		for (int i = 0; i < Math.max(a.size(), b.size()); i++) {
			if (i < a.size()) {
				send(List.of(a.get(i)));
			}
			if (i < b.size()) {
				send(List.of(b.get(i)));
			}
		}
		assertEquals(List.of("time set midnight please"), commands);
		assertEquals(List.of("a second, longer string on another channel"), shots);
	}

	@Test
	void resetDropsAHalfSentString() {
		List<String> got = new ArrayList<>();
		HostStrings.register(STR_COMMAND, got::add);
		List<int[]> first = pieces(STR_COMMAND, "this one never finishes sending");
		send(first.subList(0, 1));
		HostStrings.reset();
		send(pieces(STR_COMMAND, "say hi"));
		assertEquals(List.of("say hi"), got);
		assertTrue(first.size() > 1);
	}
}

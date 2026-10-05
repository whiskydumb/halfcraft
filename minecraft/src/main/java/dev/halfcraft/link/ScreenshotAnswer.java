package dev.halfcraft.link;

import java.nio.file.InvalidPathException;
import java.nio.file.Path;

/**
 * Half-Life's answer to a screenshot request (STR_SCREENSHOT): {@code "ok <request> <width> <height> <path>"},
 * its finished frame in a temporary file of width * height RGB8 pixels (top row first), or
 * {@code "fail <request> <reason>"}. {@code path} is null for a failure, {@code reason} for a success.
 */
public record ScreenshotAnswer(int request, int width, int height, String path, String reason) {
	/** The request number of Half-Life's own screenshots (its hc_screenshot command), which Minecraft never asked for. */
	public static final int HOST_REQUEST = 0;
	// the only files Minecraft reads and deletes for Half-Life
	private static final String FILE_PREFIX = "halfcraft-screenshot-";
	private static final String FILE_SUFFIX = ".rgb";
	private static final int MAX_SIDE = 16384;

	public boolean ok() {
		return this.path != null;
	}

	/** Size of the frame's file. */
	public long bytes() {
		return (long) this.width * this.height * 3;
	}

	/** The answer in {@code text}, or null when it isn't one. */
	public static ScreenshotAnswer parse(String text) {
		try {
			if (text.startsWith("ok ")) {
				String[] parts = text.split(" ", 5);
				if (parts.length < 5) {
					return null;
				}
				int width = Integer.parseInt(parts[2]);
				int height = Integer.parseInt(parts[3]);
				String path = parts[4];
				if (width <= 0 || height <= 0 || width > MAX_SIDE || height > MAX_SIDE || !isFrameFile(path)) {
					return null;
				}
				return new ScreenshotAnswer(Integer.parseUnsignedInt(parts[1]), width, height, path, null);
			}
			if (text.startsWith("fail ")) {
				String[] parts = text.split(" ", 3);
				return new ScreenshotAnswer(Integer.parseUnsignedInt(parts[1]), 0, 0, null, parts.length > 2 ? parts[2] : "");
			}
		} catch (NumberFormatException e) {
			return null;
		}
		return null;
	}

	/** One pixel of the frame's file as NativeImage's ABGR (opaque: the host's alpha is no colour). */
	public static int abgr(byte[] rgb, int offset) {
		return 0xFF000000 | (rgb[offset + 2] & 0xFF) << 16 | (rgb[offset + 1] & 0xFF) << 8 | rgb[offset] & 0xFF;
	}

	private static boolean isFrameFile(String path) {
		try {
			Path name = Path.of(path).getFileName();
			return name != null && name.toString().startsWith(FILE_PREFIX) && name.toString().endsWith(FILE_SUFFIX);
		} catch (InvalidPathException e) {
			return false;
		}
	}
}

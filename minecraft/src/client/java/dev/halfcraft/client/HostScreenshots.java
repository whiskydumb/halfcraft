package dev.halfcraft.client;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.client.mixin.ScreenshotAccessor;
import dev.halfcraft.link.HostLink;
import dev.halfcraft.link.HostStrings;
import dev.halfcraft.link.Proto;
import dev.halfcraft.link.ScreenshotAnswer;
import com.mojang.blaze3d.platform.NativeImage;
import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import net.fabricmc.fabric.api.client.event.lifecycle.v1.ClientTickEvents;
import net.minecraft.ChatFormatting;
import net.minecraft.client.Minecraft;
import net.minecraft.client.Screenshot;
import net.minecraft.network.chat.ClickEvent;
import net.minecraft.network.chat.Component;
import net.minecraft.util.Util;

/**
 * Minecraft's screenshot key while Half-Life is linked. Minecraft's own framebuffer only holds its
 * hand and HUD (Half-Life draws the world), so Half-Life is asked for its finished frame instead
 * (EV_SCREENSHOT), and its answer (STR_SCREENSHOT) is saved the way vanilla saves screenshots: the
 * same folder, file name and chat line. Render thread only, apart from the file work on the IO pool.
 */
public final class HostScreenshots {
	// Half-Life answers within a few frames; a request this old was lost (a level change, a stall)
	private static final long TIMEOUT_MS = 5000;
	private static final Map<Integer, Long> PENDING = new HashMap<>();
	private static int nextRequest = 1;

	private HostScreenshots() {
	}

	public static void init() {
		HostStrings.register(Proto.STR_SCREENSHOT, HostScreenshots::answer);
		ClientTickEvents.END_CLIENT_TICK.register(HostScreenshots::tick);
	}

	/**
	 * The screenshot key (Screenshot.grab). Returns true when Half-Life takes the screenshot, so
	 * vanilla's grab of Minecraft's own framebuffer must not run.
	 */
	public static boolean request(Minecraft minecraft) {
		if (!HostClient.linked()) {
			return false;
		}
		HostLink.HostState host = HostClient.sky();
		if (!host.inGame() || host.loading()) {
			HalfCraft.LOG.info("HalfCraft: screenshot key while Half-Life has no map up");
			failed(minecraft, Component.translatable("halfcraft.screenshot.no_frame"));
			return true;
		}
		int request = nextRequest;
		nextRequest = nextRequest == Integer.MAX_VALUE ? 1 : nextRequest + 1;
		PENDING.put(request, Util.getMillis() + TIMEOUT_MS);
		HostLink.pushEvent(Proto.EV_SCREENSHOT, request, 0.0F, 0.0F, 0.0F, 0.0F, 0);
		HalfCraft.LOG.info("HalfCraft: screenshot {} asked of Half-Life", request);
		return true;
	}

	private static void tick(Minecraft minecraft) {
		if (PENDING.isEmpty()) {
			return;
		}
		long now = Util.getMillis();
		PENDING.entrySet().removeIf(pending -> {
			if (now < pending.getValue()) {
				return false;
			}
			HalfCraft.LOG.warn("HalfCraft: Half-Life never answered screenshot {}", pending.getKey());
			failed(minecraft, Component.translatable("halfcraft.screenshot.no_answer"));
			return true;
		});
	}

	private static void answer(String text) {
		Minecraft minecraft = Minecraft.getInstance();
		ScreenshotAnswer answer = ScreenshotAnswer.parse(text);
		if (answer == null) {
			HalfCraft.LOG.warn("HalfCraft: unreadable screenshot answer from Half-Life: {}", text);
			return;
		}
		if (answer.request() != ScreenshotAnswer.HOST_REQUEST && PENDING.remove(answer.request()) == null) {
			HalfCraft.LOG.warn("HalfCraft: Half-Life's screenshot {} came after it was given up on; dropped", answer.request());
			if (answer.ok()) {
				Util.ioPool().execute(() -> delete(Path.of(answer.path())));
			}
			return;
		}
		if (!answer.ok()) {
			HalfCraft.LOG.warn("HalfCraft: Half-Life had no frame for screenshot {}: {}", answer.request(), answer.reason());
			failed(minecraft, Component.literal(answer.reason()));
			return;
		}
		File folder = new File(minecraft.gameDirectory, Screenshot.SCREENSHOT_DIR);
		folder.mkdir();
		File target = ScreenshotAccessor.halfcraft$getFile(folder);
		try {
			// taken now: answers that come in the same frame (one frame answers every waiting request)
			// would all get this name before the first PNG is written
			target.createNewFile();
		} catch (IOException e) {
			HalfCraft.LOG.warn("HalfCraft: couldn't create {}", target, e);
			failed(minecraft, Component.literal(String.valueOf(e.getMessage())));
			Util.ioPool().execute(() -> delete(Path.of(answer.path())));
			return;
		}
		Util.ioPool().execute(() -> save(minecraft, answer, target));
	}

	/** Half-Life's frame file into a PNG at {@code target}, like Screenshot.grab writes Minecraft's. IO pool. */
	private static void save(Minecraft minecraft, ScreenshotAnswer answer, File target) {
		Path source = Path.of(answer.path());
		try (NativeImage image = new NativeImage(answer.width(), answer.height(), false)) {
			byte[] rgb = Files.readAllBytes(source);
			if (rgb.length != answer.bytes()) {
				throw new IOException("Half-Life's frame has " + rgb.length + " bytes, not " + answer.bytes());
			}
			for (int y = 0; y < answer.height(); y++) {
				for (int x = 0; x < answer.width(); x++) {
					image.setPixelABGR(x, y, ScreenshotAnswer.abgr(rgb, (y * answer.width() + x) * 3));
				}
			}
			image.writeToFile(target);
			HalfCraft.LOG.info("HalfCraft: Half-Life's {}x{} frame saved as {}", answer.width(), answer.height(), target);
			Component name = Component.literal(target.getName())
				.withStyle(ChatFormatting.UNDERLINE)
				.withStyle(style -> style.withClickEvent(new ClickEvent.OpenFile(target.getAbsoluteFile())));
			minecraft.execute(() -> minecraft.showDebugChat(Component.translatable("screenshot.success", name)));
		} catch (Exception e) {
			HalfCraft.LOG.warn("HalfCraft: couldn't save Half-Life's frame as a screenshot", e);
			delete(target.toPath());
			minecraft.execute(() -> failed(minecraft, Component.literal(String.valueOf(e.getMessage()))));
		} finally {
			delete(source);
		}
	}

	/** Vanilla's failure line in the chat. */
	private static void failed(Minecraft minecraft, Component reason) {
		minecraft.showDebugChat(Component.translatable("screenshot.failure", reason));
	}

	private static void delete(Path file) {
		try {
			Files.deleteIfExists(file);
		} catch (IOException e) {
			HalfCraft.LOG.warn("HalfCraft: couldn't delete {}", file, e);
		}
	}
}

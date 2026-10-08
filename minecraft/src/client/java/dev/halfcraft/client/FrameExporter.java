package dev.halfcraft.client;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.renderpearl.api.buffers.GpuBuffer;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import com.mojang.renderpearl.api.textures.GpuTexture;
import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.Proto;
import dev.halfcraft.link.HostLink;
import java.lang.foreign.MemorySegment;
import net.minecraft.client.Minecraft;

/**
 * Copies Minecraft's main render target (hand + HUD + screens on a transparent background) back
 * from the GPU and publishes it to Half-Life through the overlay triple buffer.
 *
 * The copy is asynchronous: a frame is captured into one of a few staging buffers and shipped
 * once the GPU says the copy finished, typically a frame later.
 */
public final class FrameExporter {
	private static final int STAGING = 3;
	private static final int FREE = 0;
	private static final int PENDING = 1;
	private static final int READY = 2;

	private static final Staging[] staging = new Staging[STAGING];
	private static long nextFrameId = 1;
	private static boolean loggedFormat;
	// what mapping and copying the last frames out of their readback buffers took (ns), for F3
	private static final long[] readbackNs = new long[60];
	private static int readbacks;
	private static volatile int shippedWidth, shippedHeight;

	private static final class Staging {
		GpuBuffer buffer;
		int width;
		int height;
		volatile int state = FREE;
		long frameId;
	}

	private FrameExporter() {
	}

	/** What reading a frame back took on average over the last ones (ms; 0 before the first). */
	public static double readbackMs() {
		int count = Math.min(readbacks, readbackNs.length);
		long total = 0;
		for (int i = 0; i < count; i++) {
			total += readbackNs[i];
		}
		return count == 0 ? 0.0 : total / (count * 1.0e6);
	}

	public static int width() {
		return shippedWidth;
	}

	public static int height() {
		return shippedHeight;
	}

	public static void capture(Minecraft minecraft) {
		shipReadyFrames();

		RenderTarget target = minecraft.gameRenderer.mainRenderTarget();
		GpuTexture color = target.getColorTexture();
		if (color == null) {
			return;
		}
		int width = target.width;
		int height = target.height;
		if (width > Proto.MAX_OVERLAY_W || height > Proto.MAX_OVERLAY_H) {
			return;
		}
		if (!loggedFormat) {
			loggedFormat = true;
			HalfCraft.LOG.info("HalfCraft: overlay capture {}x{} format {}", width, height, color.getFormat());
		}

		Staging slot = null;
		for (Staging s : staging) {
			if (s != null && s.state == FREE) {
				slot = s;
				break;
			}
		}
		if (slot == null) {
			for (int i = 0; i < STAGING; i++) {
				if (staging[i] == null) {
					staging[i] = slot = new Staging();
					break;
				}
			}
		}
		if (slot == null) {
			return; // all staging buffers still in flight; skip this frame
		}

		long bytes = (long) width * height * 4L;
		if (slot.buffer == null || slot.width != width || slot.height != height) {
			if (slot.buffer != null) {
				slot.buffer.close();
			}
			// read on the cpu: cached memory (client storage), not the write-combined kind an integrated
			// gpu's driver picks otherwise, which the cpu reads at a fraction of the speed
			int usage = GpuBuffer.USAGE_MAP_READ | GpuBuffer.USAGE_HINT_CLIENT_STORAGE | GpuBuffer.USAGE_COPY_DST;
			slot.buffer = RenderSystem.getDevice().createBuffer(() -> "HalfCraft overlay readback", usage, bytes);
			slot.width = width;
			slot.height = height;
		}
		final Staging captured = slot;
		captured.state = PENDING;
		captured.frameId = nextFrameId++;
		RenderSystem.getDevice().createCommandEncoder().copyTextureToBuffer(color, captured.buffer, 0L, () -> captured.state = READY, 0);
	}

	/** Maps the newest finished readback and copies it into shared memory. */
	private static void shipReadyFrames() {
		Staging newest = null;
		for (Staging s : staging) {
			if (s != null && s.state == READY && (newest == null || s.frameId > newest.frameId)) {
				newest = s;
			}
		}
		if (newest == null) {
			return;
		}
		MemorySegment shm = HostLink.segment();
		if (shm != null) {
			long bytes = (long) newest.width * newest.height * 4L;
			long start = System.nanoTime();
			try (GpuBufferSlice.MappedView view = newest.buffer.map(true, false)) {
				MemorySegment src = MemorySegment.ofBuffer(view.data());
				MemorySegment.copy(src, 0, shm, HostLink.overlayBackSlotOffset(), Math.min(bytes, src.byteSize()));
			}
			readbackNs[readbacks++ % readbackNs.length] = System.nanoTime() - start;
			shippedWidth = newest.width;
			shippedHeight = newest.height;
			HostLink.publishOverlay(newest.width, newest.height, true, newest.frameId);
		}
		// Anything older than what we just shipped is useless now.
		for (Staging s : staging) {
			if (s != null && s.state == READY && s.frameId <= newest.frameId) {
				s.state = FREE;
			}
		}
	}
}

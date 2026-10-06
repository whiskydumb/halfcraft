package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;

import dev.halfcraft.world.WaterColumns;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.VarHandle;
import java.util.ArrayList;
import java.util.List;
import org.jspecify.annotations.Nullable;

/**
 * The water probes region (WaterProbeRequests and WaterProbes in the protocol): where Minecraft wants
 * Half-Life's water probed beyond the player's water grid, and client.dll's grids around those
 * places. Render thread only.
 */
public final class WaterProbes {
	private static final VarHandle INT = JAVA_INT.varHandle();

	private WaterProbes() {
	}

	/** Seqlock write of the places to probe, {x, y, z} each (at most MAX_WATER_PROBES are sent). */
	public static void writeRequests(List<double[]> at) {
		MemorySegment s = HostLink.segment();
		if (s == null) {
			return;
		}
		long b = OFF_WATER_PROBES;
		int seq = s.get(JAVA_INT, b + WPR_SEQ);
		INT.setRelease(s, b + WPR_SEQ, seq + 1);
		VarHandle.storeStoreFence();
		int count = Math.min(at.size(), MAX_WATER_PROBES);
		s.set(JAVA_INT, b + WPR_COUNT, count);
		for (int i = 0; i < count; i++) {
			for (int k = 0; k < 3; k++) {
				s.set(JAVA_FLOAT, b + WPR_AT + (i * 3L + k) * 4L, (float) at.get(i)[k]);
			}
		}
		INT.setRelease(s, b + WPR_SEQ, seq + 2);
	}

	/** A consistent copy of client.dll's probe grids, or null (no link, nothing written yet, or mid-write). */
	public static @Nullable List<WaterColumns.Grid> read() {
		MemorySegment s = HostLink.segment();
		if (s == null) {
			return null;
		}
		long b = OFF_WATER_PROBES + WP_ANSWERS;
		int cells = WATER_PROBE_SIZE * WATER_PROBE_SIZE;
		for (int attempt = 0; attempt < 16; attempt++) {
			int seq1 = (int) INT.getAcquire(s, b + WP_SEQ);
			if (seq1 == 0) {
				return null;
			}
			if ((seq1 & 1) != 0) {
				Thread.onSpinWait();
				continue;
			}
			int count = Math.clamp(s.get(JAVA_INT, b + WP_COUNT), 0, MAX_WATER_PROBES);
			List<WaterColumns.Grid> grids = new ArrayList<>(count);
			for (int i = 0; i < count; i++) {
				long p = b + WP_PROBES + i * WATER_PROBE_BYTES;
				float[] surface = new float[cells];
				for (int c = 0; c < cells; c++) {
					surface[c] = s.get(JAVA_FLOAT, p + WP_SURFACE + c * 4L);
				}
				grids.add(new WaterColumns.Grid(s.get(JAVA_INT, p + WP_ORIGIN_X), s.get(JAVA_INT, p + WP_ORIGIN_Z), WATER_PROBE_SIZE, surface,
					s.get(JAVA_FLOAT, p + WP_Y) - WATER_PROBE_DEPTH));
			}
			VarHandle.loadLoadFence();
			if ((int) INT.getAcquire(s, b + WP_SEQ) == seq1) {
				return grids;
			}
		}
		return null;
	}
}

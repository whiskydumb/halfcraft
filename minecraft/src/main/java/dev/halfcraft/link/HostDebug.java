package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;

import java.lang.foreign.MemorySegment;
import java.lang.invoke.VarHandle;
import java.nio.charset.StandardCharsets;

/**
 * What Half-Life tells Minecraft's debug screen (F3) about itself: its map, where its player is,
 * its frame rate and link state (client.dll's HostDebug), and what's under its crosshair
 * (server.dll's HostDebugServer). Both parts are seqlocked and written a few times a second.
 */
public final class HostDebug {
	private static final VarHandle INT = JAVA_INT.varHandle();

	// client.dll's part
	public int flags;
	public String map = "";
	public String chapter = "";
	public String chapterTitle = "";
	public final float[] origin = new float[3];
	public final float[] angles = new float[3];
	public float fps;
	public float worstFrameMs;
	public float overlayMs;
	public int slot;
	public int collisionEpoch;
	public int inputPending;
	public int eventPending;
	public int lightEmitters;
	public long collisionPending;
	public long renderPending;
	public int lights;
	public int shadowedLights;
	public float gridZ;

	// server.dll's part
	public boolean haveServer;
	public int entityCount;
	public int targetIndex;
	public int health;
	public int maxHealth;
	public float distance;
	public int relation;
	public int npcState;
	public String targetClass = "";
	public String targetName = "";
	public String schedule = "";

	/** Whether Minecraft drives Half-Life's player. */
	public boolean puppet() {
		return (flags & HD_PUPPET) != 0;
	}

	/** Whether keys and mouse go to Minecraft. */
	public boolean minecraftInput() {
		return (flags & HD_MINECRAFT_INPUT) != 0;
	}

	/** Whether server.dll's part is about the map being played (it stops writing outside one). */
	public boolean serverCurrent() {
		return haveServer && !map.isEmpty();
	}

	/** Whether something is under Half-Life's crosshair. */
	public boolean hasTarget() {
		return serverCurrent() && !targetClass.isEmpty();
	}

	/**
	 * Reads both parts from the mapping. Returns false (leaving {@code out} as it was) when client.dll
	 * hasn't written its part yet or every attempt was torn; server.dll's part is optional
	 * ({@link #haveServer}).
	 */
	public static boolean read(MemorySegment s, HostDebug out) {
		if (s == null || !readClient(s, OFF_HOST_DEBUG, out)) {
			return false;
		}
		out.haveServer = readServer(s, OFF_HOST_DEBUG + HD_SERVER, out);
		return true;
	}

	private static boolean readClient(MemorySegment s, long b, HostDebug out) {
		for (int attempt = 0; attempt < 16; attempt++) {
			int seq1 = (int) INT.getAcquire(s, b + HD_SEQ);
			if (seq1 == 0) {
				return false;
			}
			if ((seq1 & 1) != 0) {
				Thread.onSpinWait();
				continue;
			}
			int flags = s.get(JAVA_INT, b + HD_FLAGS);
			String map = text(s, b + HD_MAP, HD_MAP_BYTES);
			String chapter = text(s, b + HD_CHAPTER, HD_CHAPTER_BYTES);
			String chapterTitle = text(s, b + HD_CHAPTER_TITLE, HD_CHAPTER_TITLE_BYTES);
			float[] origin = new float[3];
			float[] angles = new float[3];
			for (int k = 0; k < 3; k++) {
				origin[k] = s.get(JAVA_FLOAT, b + HD_ORIGIN + 4L * k);
				angles[k] = s.get(JAVA_FLOAT, b + HD_ANGLES + 4L * k);
			}
			float fps = s.get(JAVA_FLOAT, b + HD_FPS);
			float worstFrameMs = s.get(JAVA_FLOAT, b + HD_WORST_FRAME_MS);
			float overlayMs = s.get(JAVA_FLOAT, b + HD_OVERLAY_MS);
			int slot = s.get(JAVA_INT, b + HD_SLOT);
			int collisionEpoch = s.get(JAVA_INT, b + HD_COLLISION_EPOCH);
			int inputPending = s.get(JAVA_INT, b + HD_INPUT_PENDING);
			int eventPending = s.get(JAVA_INT, b + HD_EVENT_PENDING);
			int lightEmitters = s.get(JAVA_INT, b + HD_LIGHT_EMITTERS);
			long collisionPending = s.get(JAVA_LONG, b + HD_COLLISION_PENDING);
			long renderPending = s.get(JAVA_LONG, b + HD_RENDER_PENDING);
			int lights = s.get(JAVA_INT, b + HD_LIGHTS);
			int shadowedLights = s.get(JAVA_INT, b + HD_SHADOWED_LIGHTS);
			float gridZ = s.get(JAVA_FLOAT, b + HD_GRID_Z);
			VarHandle.loadLoadFence();
			if ((int) INT.getAcquire(s, b + HD_SEQ) != seq1) {
				continue;
			}
			out.flags = flags;
			out.map = map;
			out.chapter = chapter;
			out.chapterTitle = chapterTitle;
			System.arraycopy(origin, 0, out.origin, 0, 3);
			System.arraycopy(angles, 0, out.angles, 0, 3);
			out.fps = fps;
			out.worstFrameMs = worstFrameMs;
			out.overlayMs = overlayMs;
			out.slot = slot;
			out.collisionEpoch = collisionEpoch;
			out.inputPending = inputPending;
			out.eventPending = eventPending;
			out.lightEmitters = lightEmitters;
			out.collisionPending = collisionPending;
			out.renderPending = renderPending;
			out.lights = lights;
			out.shadowedLights = shadowedLights;
			out.gridZ = gridZ;
			return true;
		}
		return false;
	}

	private static boolean readServer(MemorySegment s, long b, HostDebug out) {
		for (int attempt = 0; attempt < 16; attempt++) {
			int seq1 = (int) INT.getAcquire(s, b + HDS_SEQ);
			if (seq1 == 0) {
				return false;
			}
			if ((seq1 & 1) != 0) {
				Thread.onSpinWait();
				continue;
			}
			int entityCount = s.get(JAVA_INT, b + HDS_ENTITY_COUNT);
			int targetIndex = s.get(JAVA_INT, b + HDS_TARGET_INDEX);
			int health = s.get(JAVA_INT, b + HDS_HEALTH);
			int maxHealth = s.get(JAVA_INT, b + HDS_MAX_HEALTH);
			float distance = s.get(JAVA_FLOAT, b + HDS_DISTANCE);
			int relation = s.get(JAVA_INT, b + HDS_RELATION);
			int npcState = s.get(JAVA_INT, b + HDS_NPC_STATE);
			String targetClass = text(s, b + HDS_TARGET_CLASS, HDS_TEXT_BYTES);
			String targetName = text(s, b + HDS_TARGET_NAME, HDS_TEXT_BYTES);
			String schedule = text(s, b + HDS_SCHEDULE, HDS_TEXT_BYTES);
			VarHandle.loadLoadFence();
			if ((int) INT.getAcquire(s, b + HDS_SEQ) != seq1) {
				continue;
			}
			out.entityCount = entityCount;
			out.targetIndex = targetIndex;
			out.health = health;
			out.maxHealth = maxHealth;
			out.distance = distance;
			out.relation = relation;
			out.npcState = npcState;
			out.targetClass = targetClass;
			out.targetName = targetName;
			out.schedule = schedule;
			return true;
		}
		return false;
	}

	/** A NUL-terminated UTF-8 field of at most {@code max} bytes. */
	private static String text(MemorySegment s, long off, int max) {
		byte[] bytes = new byte[max];
		int n = 0;
		while (n < max) {
			byte b = s.get(JAVA_BYTE, off + n);
			if (b == 0) {
				break;
			}
			bytes[n++] = b;
		}
		return new String(bytes, 0, n, StandardCharsets.UTF_8);
	}
}

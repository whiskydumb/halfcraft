package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;

import dev.halfcraft.HalfCraft;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.VarHandle;
import java.nio.charset.StandardCharsets;

/**
 * The Minecraft end of the shared-memory link. Half-Life owns the mapping; we open it when it
 * appears and treat it as gone when Half-Life's heartbeat stops.
 */
public final class HostLink {
	private static final int FILE_MAP_ALL_ACCESS = 0xF001F;
	// Half-Life's loading screens can stall its heartbeat for several seconds.
	private static final long HEARTBEAT_TIMEOUT_MS = 8000;

	private static final VarHandle INT = JAVA_INT.varHandle();
	private static final VarHandle LONG = JAVA_LONG.varHandle();

	private static final MethodHandle OPEN_FILE_MAPPING;
	// OpenFileMappingW's GetLastError, captured right after the call (the JVM may change it later).
	private static final java.lang.foreign.StructLayout CALL_STATE = Linker.Option.captureStateLayout();
	private static final VarHandle LAST_ERROR = CALL_STATE.varHandle(java.lang.foreign.MemoryLayout.PathElement.groupElement("GetLastError"));
	private static final MemorySegment OPEN_STATE = Arena.global().allocate(CALL_STATE);
	private static int lastOpenError = -1;
	private static final MethodHandle MAP_VIEW_OF_FILE;
	private static final MethodHandle GET_TICK_COUNT64;
	private static final MethodHandle GET_CURRENT_PROCESS_ID;
	private static final MethodHandle QUERY_PERFORMANCE_COUNTER;
	private static final MethodHandle QUERY_PERFORMANCE_FREQUENCY;
	private static final MethodHandle CREATE_MUTEX;
	private static final MethodHandle OPEN_EVENT;
	private static final MethodHandle WAIT_FOR_SINGLE_OBJECT;
	private static final int SYNCHRONIZE = 0x00100000;
	private static MemorySegment runningMutex;
	// Half-Life's frame event (Proto.FRAME_EVENT_NAME); null before it's open, or from a Half-Life without one
	private static volatile MemorySegment frameEvent;
	private static final MemorySegment QPC_OUT = Arena.global().allocate(JAVA_LONG);

	static {
		Linker linker = Linker.nativeLinker();
		SymbolLookup k32 = SymbolLookup.libraryLookup("kernel32", Arena.global());
		OPEN_FILE_MAPPING = linker.downcallHandle(
			k32.find("OpenFileMappingW").orElseThrow(), FunctionDescriptor.of(ADDRESS, JAVA_INT, JAVA_INT, ADDRESS), Linker.Option.captureCallState("GetLastError")
		);
		MAP_VIEW_OF_FILE = linker.downcallHandle(
			k32.find("MapViewOfFile").orElseThrow(), FunctionDescriptor.of(ADDRESS, ADDRESS, JAVA_INT, JAVA_INT, JAVA_INT, JAVA_LONG)
		);
		GET_TICK_COUNT64 = linker.downcallHandle(k32.find("GetTickCount64").orElseThrow(), FunctionDescriptor.of(JAVA_LONG));
		GET_CURRENT_PROCESS_ID = linker.downcallHandle(k32.find("GetCurrentProcessId").orElseThrow(), FunctionDescriptor.of(JAVA_INT));
		QUERY_PERFORMANCE_COUNTER = linker.downcallHandle(k32.find("QueryPerformanceCounter").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS));
		QUERY_PERFORMANCE_FREQUENCY = linker.downcallHandle(k32.find("QueryPerformanceFrequency").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS));
		CREATE_MUTEX = linker.downcallHandle(k32.find("CreateMutexW").orElseThrow(), FunctionDescriptor.of(ADDRESS, ADDRESS, JAVA_INT, ADDRESS));
		OPEN_EVENT = linker.downcallHandle(k32.find("OpenEventW").orElseThrow(), FunctionDescriptor.of(ADDRESS, JAVA_INT, JAVA_INT, ADDRESS));
		WAIT_FOR_SINGLE_OBJECT = linker.downcallHandle(k32.find("WaitForSingleObject").orElseThrow(), FunctionDescriptor.of(JAVA_INT, ADDRESS, JAVA_INT));
	}

	/**
	 * Sleeps until Half-Life finishes a frame (it sets its frame event after each HostState) or ms pass.
	 * False without the event (a Half-Life before it): the caller naps and polls instead.
	 */
	public static boolean waitForHostFrame(int ms) {
		MemorySegment event = frameEvent;
		if (event == null) {
			return false;
		}
		try {
			int result = (int) WAIT_FOR_SINGLE_OBJECT.invokeExact(event, ms);
			return result != -1; // WAIT_FAILED
		} catch (Throwable t) {
			return false;
		}
	}

	// the frame event, once the mapping is open: it outlives a Half-Life restart while this holds it, as the mapping does
	private static void openFrameEvent() {
		if (frameEvent != null) {
			return;
		}
		try (Arena arena = Arena.ofConfined()) {
			MemorySegment name = arena.allocateFrom(FRAME_EVENT_NAME, StandardCharsets.UTF_16LE);
			MemorySegment handle = (MemorySegment) OPEN_EVENT.invokeExact(SYNCHRONIZE, 0, name);
			if (handle.address() != 0) {
				frameEvent = handle;
				HalfCraft.LOG.info("HalfCraft: waiting on Half-Life's frame event between frames");
			}
		} catch (Throwable t) {
			HalfCraft.LOG.warn("HalfCraft: couldn't open Half-Life's frame event", t);
		}
	}

	/**
	 * Holds a named mutex ("<link name>_minecraft") for as long as this Minecraft runs, so a launcher
	 * knows not to start another one (even before the two games have linked up).
	 */
	public static synchronized void announceRunning() {
		if (runningMutex != null) {
			return;
		}
		try (Arena arena = Arena.ofConfined()) {
			MemorySegment name = arena.allocateFrom(MAPPING_NAME + "_minecraft", StandardCharsets.UTF_16LE);
			runningMutex = (MemorySegment) CREATE_MUTEX.invokeExact(MemorySegment.NULL, 0, name);
		} catch (Throwable t) {
			HalfCraft.LOG.warn("HalfCraft: couldn't create the running-Minecraft mutex", t);
		}
	}

	private static volatile MemorySegment shm;
	private static long lastOpenAttempt;
	private static int hostPid;
	private static long hostSession;
	private static volatile int generation;

	private HostLink() {
	}

	/** True when a live Half-Life is on the other end. Cheap; safe from any thread. */
	public static boolean active() {
		MemorySegment s = shm;
		if (s == null) {
			return false;
		}
		long beat = (long) LONG.getAcquire(s, OFF_HEADER + H_HOST_HEARTBEAT);
		return tickCount() - beat < HEARTBEAT_TIMEOUT_MS;
	}

	/** Bumps whenever a (new) Half-Life instance is on the other end: everything Half-Life caches must be resent. */
	public static int generation() {
		return generation;
	}

	/** Process id of the Half-Life we're linked to (0 before the first link). */
	public static int hostPid() {
		return hostPid;
	}

	/** The mapping if it has been opened (whether or not Half-Life is still alive). */
	public static MemorySegment segment() {
		return shm;
	}

	/** Try to open the mapping at most once a second. Call regularly from the render thread. */
	public static void poll() {
		if (shm != null) {
			if (frameEvent == null && System.currentTimeMillis() - lastOpenAttempt >= 1000) {
				lastOpenAttempt = System.currentTimeMillis();
				openFrameEvent();
			}
			LONG.setRelease(shm, OFF_HEADER + H_MC_HEARTBEAT, tickCount());
			long session = (long) LONG.getAcquire(shm, OFF_HEADER + H_HOST_SESSION);
			if (session != hostSession) {
				// Half-Life restarted and reset the shared state; start our side over too. Its session,
				// not its pid: a restarted Half-Life can get the pid of the one before it.
				hostSession = session;
				hostPid = shm.get(JAVA_INT, OFF_HEADER + H_HOST_PID);
				overlayBack = 1;
				HostStrings.reset();
				generation++;
				HalfCraft.LOG.info("HalfCraft: Half-Life instance changed (pid {})", hostPid);
			}
			return;
		}
		long now = System.currentTimeMillis();
		if (now - lastOpenAttempt < 1000) {
			return;
		}
		lastOpenAttempt = now;
		try (Arena arena = Arena.ofConfined()) {
			MemorySegment name = arena.allocateFrom(MAPPING_NAME, StandardCharsets.UTF_16LE);
			MemorySegment handle = (MemorySegment) OPEN_FILE_MAPPING.invokeExact(OPEN_STATE, FILE_MAP_ALL_ACCESS, 0, name);
			if (handle.address() == 0) {
				// Say why, once per reason: 2 is "Half-Life hasn't made it yet" (normal while it loads),
				// 5 is "not allowed" (a Half-Life run as administrator, before 0.1.1).
				int error = (int) LAST_ERROR.get(OPEN_STATE, 0L);
				if (error != lastOpenError) {
					lastOpenError = error;
					HalfCraft.LOG.info("HalfCraft: can't open Half-Life's shared memory yet (Windows error {}{})", error,
						error == 2 ? ": Half-Life hasn't created it yet" : error == 5 ? ": access denied; is Half-Life running as administrator?" : "");
				}
				return;
			}
			MemorySegment view = (MemorySegment) MAP_VIEW_OF_FILE.invokeExact(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0L);
			if (view.address() == 0) {
				HalfCraft.LOG.error("HalfCraft: MapViewOfFile failed");
				return;
			}
			MemorySegment seg = view.reinterpret(MAPPING_BYTES);
			int magic = seg.get(JAVA_INT, OFF_HEADER + H_MAGIC);
			int layout = seg.get(JAVA_INT, OFF_HEADER + H_LAYOUT);
			if (magic != MAGIC || layout != LAYOUT) {
				HalfCraft.LOG.error("HalfCraft: Half-Life's shared memory has another layout ({}, magic {}) than this mod's ({}): build both from the same commit",
					Integer.toHexString(layout), Integer.toHexString(magic), Integer.toHexString(LAYOUT));
				return;
			}
			seg.set(JAVA_INT, OFF_HEADER + H_MC_PID, (int) GET_CURRENT_PROCESS_ID.invokeExact());
			LONG.setRelease(seg, OFF_HEADER + H_MC_HEARTBEAT, tickCount());
			hostPid = seg.get(JAVA_INT, OFF_HEADER + H_HOST_PID);
			hostSession = (long) LONG.getAcquire(seg, OFF_HEADER + H_HOST_SESSION);
			generation++;
			shm = seg;
			HalfCraft.LOG.info("HalfCraft: linked to Half-Life (pid {})", seg.get(JAVA_INT, OFF_HEADER + H_HOST_PID));
		} catch (Throwable t) {
			HalfCraft.LOG.error("HalfCraft: failed to open shared memory", t);
		}
	}

	/** QueryPerformanceCounter: the same clock Half-Life reads, so tick timestamps line up across processes. */
	public static synchronized long qpc() {
		try {
			int ok = (int) QUERY_PERFORMANCE_COUNTER.invokeExact(QPC_OUT);
			return QPC_OUT.get(JAVA_LONG, 0);
		} catch (Throwable t) {
			throw new RuntimeException(t);
		}
	}

	/** Ticks per second of {@link #qpc()}. */
	public static synchronized long qpcFrequency() {
		try {
			int ok = (int) QUERY_PERFORMANCE_FREQUENCY.invokeExact(QPC_OUT);
			return QPC_OUT.get(JAVA_LONG, 0);
		} catch (Throwable t) {
			throw new RuntimeException(t);
		}
	}

	public static long tickCount() {
		try {
			return (long) GET_TICK_COUNT64.invokeExact();
		} catch (Throwable t) {
			throw new RuntimeException(t);
		}
	}

	// ---- HostState (read) -------------------------------------------------------------------

	/** Plain snapshot of HostState, and the sequence number it was read at. */
	public static final class HostState extends ProtoStructs.HostState {
		public int seq;

		public boolean inGame() {
			return (this.flags & HOST_IN_GAME) != 0;
		}

		public boolean menuOpen() {
			return (this.flags & HOST_MENU_OPEN) != 0;
		}

		public boolean loading() {
			return (this.flags & HOST_LOADING) != 0;
		}

		/** Half-Life moves its player itself (a ladder, a lift, a vehicle): Minecraft's player goes where it is. */
		public boolean takeover() {
			return (this.flags & HOST_TAKEOVER) != 0;
		}

		/** In a Half-Life vehicle's seat, which faces {@link #seatYaw}. */
		public boolean seated() {
			return (this.flags & HOST_SEATED) != 0;
		}

		/**
		 * How much smaller than Half-Life's viewport the overlay is drawn (hc_overlay_scale), its gui scale
		 * alike; Half-Life scales it back up. 1 from a Half-Life before it.
		 */
		public int divisor() {
			return Math.max(1, this.overlayDivisor);
		}
	}

	/** Half-Life's water surface around the player (see WaterGrid in the protocol). */
	public static final class WaterGrid extends ProtoStructs.WaterGrid {
		public final int size = WATER_GRID_SIZE;
	}

	/** A consistent copy of the water grid, or null (no link, or Half-Life mid-write). */
	public static WaterGrid readWaterGrid() {
		MemorySegment s = shm;
		if (s == null) {
			return null;
		}
		long b = OFF_WATER_GRID;
		WaterGrid out = new WaterGrid();
		for (int attempt = 0; attempt < 100; attempt++) {
			int seq1 = (int) INT.getAcquire(s, b + WG_SEQ);
			if ((seq1 & 1) != 0 || seq1 == 0) {
				Thread.onSpinWait();
				if (seq1 == 0) {
					return null; // Half-Life hasn't written one yet
				}
				continue;
			}
			out.read(s, b);
			VarHandle.loadLoadFence();
			if ((int) INT.getAcquire(s, b + WG_SEQ) == seq1) {
				return out;
			}
		}
		return null;
	}

	public static boolean readHostState(HostState out) {
		MemorySegment s = shm;
		if (s == null) {
			return false;
		}
		long b = OFF_HOST_STATE;
		for (int attempt = 0; attempt < 1000; attempt++) {
			int seq1 = (int) INT.getAcquire(s, b + HS_SEQ);
			if ((seq1 & 1) != 0) {
				if (attempt > 100) {
					Thread.yield();
				} else {
					Thread.onSpinWait();
				}
				continue;
			}
			out.read(s, b);
			VarHandle.loadLoadFence();
			int seq2 = (int) INT.getAcquire(s, b + HS_SEQ);
			if (seq1 == seq2) {
				out.seq = seq1;
				return true;
			}
		}
		return false;
	}

	/** Raw HostState sequence number; changes once per Half-Life frame. */
	public static int hostStateSeq() {
		MemorySegment s = shm;
		return s == null ? 0 : (int) INT.getAcquire(s, OFF_HOST_STATE + HS_SEQ);
	}

	// ---- McState (write) -------------------------------------------------------------------

	public static final class McState extends ProtoStructs.McState {
		public McState() {
			this.tickMs = 50.0F;
		}
	}

	public static void writeMcState(McState st) {
		MemorySegment s = shm;
		if (s == null) {
			return;
		}
		long b = OFF_MC_STATE;
		int seq = s.get(JAVA_INT, b + MS_SEQ);
		INT.setRelease(s, b + MS_SEQ, seq + 1);
		VarHandle.storeStoreFence();
		st.write(s, b);
		INT.setRelease(s, b + MS_SEQ, seq + 2);
	}

	// ---- input ring (consume) --------------------------------------------------------------

	public interface InputSink {
		void accept(int type, int code, int a, int b, int c);
	}

	/** Drains every pending input event. Render thread only. */
	public static void drainInput(InputSink sink) {
		MemorySegment s = shm;
		if (s == null) {
			return;
		}
		long base = OFF_INPUT_RING;
		long head = (long) LONG.getAcquire(s, base + IR_HEAD);
		long tail = s.get(JAVA_LONG, base + IR_TAIL);
		if (tail > head) {
			HalfCraft.LOG.warn("HalfCraft: input ring out of step (written up to {}, read up to {}): resyncing", head, tail);
			tail = head; // read past what was written: wait at Half-Life's head from now on
		} else if (head - tail > INPUT_RING_ENTRIES) {
			tail = head - INPUT_RING_ENTRIES; // producer lapped us; drop the oldest
		}
		while (tail < head) {
			ProtoStructs.InputEvent event = ProtoStructs.InputEvent.read(s, base + IR_DATA + (tail & (INPUT_RING_ENTRIES - 1)) * INPUT_EVENT_BYTES);
			tail++;
			sink.accept(event.type(), event.code(), event.a(), event.b(), event.c());
		}
		LONG.setRelease(s, base + IR_TAIL, tail);
	}

	// ---- actor table (read) ----------------------------------------------------------------

	/** What one nearby Half-Life actor's flags say (ProtoStructs.ActorRecord, the actor table's records, has it). */
	public interface Actor {
		int flags();

		default boolean dead() {
			return (this.flags() & ACTOR_DEAD) != 0;
		}

		default boolean hostile() {
			return (this.flags() & ACTOR_HOSTILE) != 0;
		}
	}

	/** Seqlock read of the actor table. Returns false (leaving {@code out} empty) on a torn read. */
	public static boolean readActors(java.util.List<ProtoStructs.ActorRecord> out) {
		out.clear();
		MemorySegment s = shm;
		if (s == null) {
			return false;
		}
		long b = OFF_ACTOR_TABLE;
		for (int attempt = 0; attempt < 16; attempt++) {
			int seq1 = (int) INT.getAcquire(s, b + AT_SEQ);
			if ((seq1 & 1) != 0) {
				Thread.onSpinWait();
				continue;
			}
			int count = Math.min(s.get(JAVA_INT, b + AT_COUNT), MAX_ACTORS);
			for (int i = 0; i < count; i++) {
				out.add(ProtoStructs.ActorRecord.read(s, b + AT_RECORDS + i * ACTOR_RECORD_BYTES));
			}
			VarHandle.loadLoadFence();
			if ((int) INT.getAcquire(s, b + AT_SEQ) == seq1) {
				return true;
			}
			out.clear();
		}
		return false;
	}

	// ---- event ring (produce) --------------------------------------------------------------

	/** Queues an event for Half-Life. Safe from any thread. Drops the event if Half-Life is a full ring behind. */
	public static void pushEvent(int type, int actorId, float a, float b, float c, float d, int flags) {
		pushEvent(type, actorId, a, b, c, d, flags, 0);
	}

	public static void pushEvent(int type, int actorId, float a, float b, float c, float d, int flags, int weapon) {
		pushEvent(type, actorId, a, b, c, d, flags, weapon, 0);
	}

	public static synchronized void pushEvent(int type, int actorId, float a, float b, float c, float d, int flags, int weapon, int attackerId) {
		MemorySegment s = shm;
		if (s == null) {
			return;
		}
		long base = OFF_EVENT_RING;
		long head = s.get(JAVA_LONG, base + ER_HEAD);
		long tail = (long) LONG.getAcquire(s, base + ER_TAIL);
		if (head - tail >= EVENT_RING_ENTRIES) {
			return;
		}
		long e = base + ER_DATA + (head & (EVENT_RING_ENTRIES - 1)) * EVENT_BYTES;
		new ProtoStructs.McEvent(type, actorId, a, b, c, d, flags, weapon, attackerId).write(s, e);
		LONG.setRelease(s, base + ER_HEAD, head + 1);
	}

	// ---- world entities (write) ------------------------------------------------------------

	/**
	 * Seqlock write of the world-entity table and block selection. Render thread only. An entity's
	 * {@code uv} holds up to three atlas rects {u0, v0, u1, v1}: sprite/side, top, bottom.
	 */
	public static void writeWorldEntities(java.util.List<ProtoStructs.WorldEntity> entities, float[] selection) {
		MemorySegment s = shm;
		if (s == null) {
			return;
		}
		long b = OFF_WORLD_ENTITIES;
		int seq = s.get(JAVA_INT, b + WE_SEQ);
		INT.setRelease(s, b + WE_SEQ, seq + 1);
		VarHandle.storeStoreFence();
		int count = Math.min(entities.size(), MAX_WORLD_ENTITIES);
		s.set(JAVA_INT, b + WE_COUNT, count);
		s.set(JAVA_INT, b + WE_HAS_SELECTION, selection != null ? 1 : 0);
		if (selection != null) {
			for (int i = 0; i < 6; i++) {
				s.set(JAVA_FLOAT, b + WE_SEL_MIN + i * 4L, selection[i]);
			}
		}
		for (int i = 0; i < count; i++) {
			entities.get(i).write(s, b + WE_RECORDS + i * WORLD_ENTITY_BYTES);
		}
		INT.setRelease(s, b + WE_SEQ, seq + 2);
	}

	// ---- render ring (produce) -------------------------------------------------------------

	/**
	 * Writes one render message ({@code header} bytes then {@code body} bytes) into the render ring,
	 * waiting briefly for space. Single producer. Returns false if it never fit.
	 */
	public static boolean writeRender(int type, java.nio.ByteBuffer header, java.nio.ByteBuffer body) {
		return writeRender(type, header, body, 500);
	}

	/** Like writeRender, but gives up at once if the ring is full (per-frame data that the next frame replaces). */
	public static boolean tryWriteRender(int type, java.nio.ByteBuffer header, java.nio.ByteBuffer body) {
		return writeRender(type, header, body, 1);
	}

	private static synchronized boolean writeRender(int type, java.nio.ByteBuffer header, java.nio.ByteBuffer body, int attempts) {
		MemorySegment s = shm;
		if (s == null) {
			return false;
		}
		int payload = header.remaining() + (body != null ? body.remaining() : 0);
		long msgBytes = (8 + payload + 7) & ~7L;
		if (msgBytes > RR_DATA_BYTES / 2) {
			HalfCraft.LOG.warn("HalfCraft: render message too large ({} bytes)", msgBytes);
			return false;
		}
		long base = OFF_RENDER_RING;
		for (int attempt = 0; attempt < attempts; attempt++) {
			long head = s.get(JAVA_LONG, base + RR_HEAD);
			long tail = (long) LONG.getAcquire(s, base + RR_TAIL);
			long pos = head % RR_DATA_BYTES;
			long pad = pos + msgBytes > RR_DATA_BYTES ? RR_DATA_BYTES - pos : 0;
			if (RR_DATA_BYTES - (head - tail) < msgBytes + pad) {
				if (attempt + 1 >= attempts) {
					break;
				}
				try {
					Thread.sleep(2);
				} catch (InterruptedException e) {
					return false;
				}
				continue;
			}
			if (pad > 0) {
				s.set(JAVA_INT, base + RR_DATA + pos, REN_PAD);
				s.set(JAVA_INT, base + RR_DATA + pos + 4, 0);
				head += pad;
				pos = 0;
			}
			long at = base + RR_DATA + pos;
			s.set(JAVA_INT, at, type);
			s.set(JAVA_INT, at + 4, payload);
			MemorySegment.copy(MemorySegment.ofBuffer(header), 0, s, at + 8, header.remaining());
			if (body != null && body.remaining() > 0) {
				MemorySegment.copy(MemorySegment.ofBuffer(body), 0, s, at + 8 + header.remaining(), body.remaining());
			}
			LONG.setRelease(s, base + RR_HEAD, head + msgBytes);
			return true;
		}
		return false;
	}

	// ---- overlay (publish) -----------------------------------------------------------------

	private static int overlayBack = 1; // writer's private slot; Half-Life's front starts at 2, middle at 0

	/** Returns the byte offset where the next overlay frame should be written. */
	public static long overlayBackSlotOffset() {
		return OFF_OVERLAY_PIXELS + overlayBack * OVERLAY_SLOT_BYTES;
	}

	/** Publishes the frame just written into the back slot. */
	public static void publishOverlay(int width, int height, boolean bottomUp, long frameId) {
		MemorySegment s = shm;
		if (s == null) {
			return;
		}
		new ProtoStructs.OverlaySlotHdr(width, height, bottomUp ? 1 : 0, frameId).write(s, OFF_OVERLAY_SLOT_HDR + overlayBack * SLOT_HDR_SIZE);
		int old = (int) INT.getAndSet(s, OFF_OVERLAY_CTL + OC_STATE, overlayBack | OVERLAY_DIRTY);
		overlayBack = old & 3;
		LONG.getAndAdd(s, OFF_OVERLAY_CTL + OC_FRAMES_PUBLISHED, 1L);
	}

	// ---- collision ring (consume) ----------------------------------------------------------

	public static long collisionHead() {
		MemorySegment s = shm;
		return s == null ? 0 : (long) LONG.getAcquire(s, OFF_COLLISION_RING + CR_HEAD);
	}

	public static long collisionTail() {
		MemorySegment s = shm;
		return s == null ? 0 : s.get(JAVA_LONG, OFF_COLLISION_RING + CR_TAIL);
	}

	public static void setCollisionTail(long tail) {
		MemorySegment s = shm;
		if (s != null) {
			LONG.setRelease(s, OFF_COLLISION_RING + CR_TAIL, tail);
		}
	}
}

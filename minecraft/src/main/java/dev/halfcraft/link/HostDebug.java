package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;

import java.lang.foreign.MemorySegment;
import java.lang.invoke.VarHandle;
import org.jspecify.annotations.Nullable;

/**
 * What Half-Life tells Minecraft's debug screen (F3) about itself: its map, where its player is,
 * its frame rate and link state (client.dll's HostDebug: these fields), and what's under its
 * crosshair (server.dll's HostDebugServer: {@link #server}). Both parts are seqlocked and written a
 * few times a second.
 */
public final class HostDebug extends ProtoStructs.HostDebug {
	private static final VarHandle INT = JAVA_INT.varHandle();

	// server.dll's part, as last read
	public ProtoStructs.HostDebugServer server = new ProtoStructs.HostDebugServer();
	public boolean haveServer;

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
		return serverCurrent() && !server.targetClass.isEmpty();
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
		ProtoStructs.HostDebugServer server = readServer(s, OFF_HOST_DEBUG + HD_SERVER);
		out.haveServer = server != null;
		if (server != null) {
			out.server = server;
		}
		return true;
	}

	private static boolean readClient(MemorySegment s, long b, HostDebug out) {
		ProtoStructs.HostDebug part = new ProtoStructs.HostDebug();
		for (int attempt = 0; attempt < 16; attempt++) {
			int seq1 = (int) INT.getAcquire(s, b + HD_SEQ);
			if (seq1 == 0) {
				return false;
			}
			if ((seq1 & 1) != 0) {
				Thread.onSpinWait();
				continue;
			}
			part.read(s, b);
			VarHandle.loadLoadFence();
			if ((int) INT.getAcquire(s, b + HD_SEQ) == seq1) {
				out.copyFrom(part);
				return true;
			}
		}
		return false;
	}

	private static ProtoStructs.@Nullable HostDebugServer readServer(MemorySegment s, long b) {
		ProtoStructs.HostDebugServer part = new ProtoStructs.HostDebugServer();
		for (int attempt = 0; attempt < 16; attempt++) {
			int seq1 = (int) INT.getAcquire(s, b + HDS_SEQ);
			if (seq1 == 0) {
				return null;
			}
			if ((seq1 & 1) != 0) {
				Thread.onSpinWait();
				continue;
			}
			part.read(s, b);
			VarHandle.loadLoadFence();
			if ((int) INT.getAcquire(s, b + HDS_SEQ) == seq1) {
				return part;
			}
		}
		return null;
	}
}

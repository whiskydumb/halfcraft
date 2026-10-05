package dev.halfcraft.link;

import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Minecraft's own teleports of its player (an ender pearl, chorus fruit, /tp, the server putting the
 * player back), counted for McState's teleportCount: Half-Life takes the jump that follows wherever
 * its player fits, instead of undoing it. Half-Life's own teleports (the client putting the player
 * where Half-Life has it) don't count.
 *
 * <p>The integrated server marks the position packets of Minecraft's own teleports as it sends them
 * ({@link #serverSent}); the client counts a marked one once it has applied it ({@link #clientApplied}),
 * so the count and the new position reach Half-Life in the same tick.
 */
public final class HostTeleports {
	// marked packets the client hasn't applied yet; more than this many means some never will be (a
	// disconnect while they were on their way)
	static final int MAX_PENDING = 16;

	// teleport ids, oldest first; guarded by itself (the server thread adds, the render thread takes)
	private static final Deque<Integer> PENDING = new ArrayDeque<>();
	// server thread only: Half-Life's teleport is being made
	private static boolean byHost;
	// render thread only
	private static int count;

	private HostTeleports() {
	}

	/** Server thread: runs Half-Life's own teleport of the player, which Minecraft doesn't count. */
	public static void byHost(Runnable teleport) {
		byHost = true;
		try {
			teleport.run();
		} finally {
			byHost = false;
		}
	}

	/** Server thread, just before a position packet goes to the player: marks it unless it is Half-Life's. */
	public static void serverSent(int teleportId) {
		if (byHost) {
			return;
		}
		synchronized (PENDING) {
			if (PENDING.size() >= MAX_PENDING) {
				PENDING.removeFirst();
			}
			PENDING.addLast(teleportId);
		}
	}

	/**
	 * Render thread, after the client handled a position packet.
	 *
	 * @param moved the packet moved the player (a passenger stays where its vehicle is)
	 * @return the packet was one of Minecraft's own teleports, and it moved the player: counted
	 */
	public static boolean clientApplied(int teleportId, boolean moved) {
		boolean own;
		synchronized (PENDING) {
			own = PENDING.remove(teleportId);
		}
		if (own && moved) {
			count++;
		}
		return own && moved;
	}

	/** How many of Minecraft's own teleports the client has applied (wraps around). */
	public static int count() {
		return count;
	}

	/** Tests: starts over. */
	static void reset() {
		synchronized (PENDING) {
			PENDING.clear();
		}
		byHost = false;
		count = 0;
	}
}

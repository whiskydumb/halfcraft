package dev.halfcraft.link;

import org.jspecify.annotations.Nullable;

/**
 * Half-Life pushing its player while Minecraft drives it (a trigger_push, a conveyor belt, a
 * point_push: Source's base velocity), as {@link Proto#IN_PUSH} says. Minecraft's player moves along
 * by it every tick, on top of its own movement, and keeps it as momentum when Half-Life says it
 * stopped (Source turns a base velocity that ends into the player's own velocity: a trigger_push
 * throws you off its end). Half-Life repeats a push every {@link Proto#PUSH_REPEAT_MS} while it lasts,
 * so one not heard of for {@link Proto#PUSH_STALE_MS} is over without any momentum (Half-Life closed,
 * or Minecraft missed the stop). Pure logic: the caller passes the clock.
 */
public final class HostPush {
	/** The one Half-Life's input feeds (render thread). */
	public static final HostPush INSTANCE = new HostPush();

	// IN_PUSH carries blocks per second * 1000
	private static final double SCALE = 1000.0;

	private final double[] velocity = new double[3];  // blocks per second, Minecraft axes
	private long heardAt;
	private boolean pushing;
	private double @Nullable [] released;

	/** An IN_PUSH: a/b/c along Minecraft's x/y/z, (0, 0, 0) when it stopped. */
	public void accept(int a, int b, int c, long nowMs) {
		boolean now = a != 0 || b != 0 || c != 0;
		if (this.pushing && !now) {
			this.released = this.velocity.clone();
		}
		this.velocity[0] = a / SCALE;
		this.velocity[1] = b / SCALE;
		this.velocity[2] = c / SCALE;
		this.pushing = now;
		this.heardAt = nowMs;
		if (now) {
			this.released = null;
		}
	}

	/** The push now (blocks per second, Minecraft's x/y/z), or null when there is none. */
	public double @Nullable [] velocity(long nowMs) {
		if (this.pushing && nowMs - this.heardAt > Proto.PUSH_STALE_MS) {
			this.pushing = false;
		}
		return this.pushing ? this.velocity.clone() : null;
	}

	/** The push that just stopped, once (blocks per second): the player keeps it as momentum. Null: none. */
	public double @Nullable [] takeReleased() {
		double[] out = this.released;
		this.released = null;
		return out;
	}
}

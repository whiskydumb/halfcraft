package dev.halfcraft.client;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.HostLink;
import dev.halfcraft.link.HostPush;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.MoverType;
import net.minecraft.world.phys.Vec3;

/**
 * How Half-Life moves Minecraft's player besides Minecraft's own physics, every tick in place of (or
 * after) its travel.
 *
 * <p>While Half-Life moves its player itself (a ladder, a lift or train, a vehicle, a scripted scene:
 * HostState's takeover flag), Minecraft's player goes where Half-Life's is instead of running its own
 * physics: it can't fall behind a lift going down, end up under one going up, or take fall damage
 * from the ride (it reports standing on ground, so the server never counts a fall). Its hands still
 * work. Half-Life hands the player back with a teleport, and Minecraft's physics pick up from there.
 *
 * <p>While Minecraft drives the player, Half-Life's pushes on it ({@link HostPush}: trigger_push,
 * conveyors, point_push) move it along, on top of its own movement, and leave it their momentum when
 * they stop; its shoves (a trigger_push that pushes once, an antlion guard) give it momentum at once.
 */
public final class HostTakeover {
	private static boolean following;
	private static boolean pushed;

	private HostTakeover() {
	}

	/** Before the local player's travel: true when Half-Life has the player and it was put there instead. */
	public static boolean follow(LocalPlayer player) {
		HostLink.HostState sky = HostClient.sky();
		boolean now = HostClient.linked() && sky.takeover() && sky.inGame() && !sky.loading() && !player.isDeadOrDying() && !player.isPassenger();
		if (now != following) {
			following = now;
			if (now) {
				HalfCraft.LOG.info("HalfCraft: Half-Life moves the player itself: Minecraft's player follows it");
			} else {
				HalfCraft.LOG.info("HalfCraft: Half-Life stopped moving the player");
			}
		}
		if (!now) {
			return false;
		}
		// a push that ended as Half-Life took over is no momentum for the hand-back, nor a shove meanwhile
		HostPush.INSTANCE.takeReleased();
		HostPush.INSTANCE.takeImpulse();
		player.setDeltaMovement(Vec3.ZERO);
		player.setPos(sky.x, sky.y, sky.z);
		player.setOnGround(true);
		player.resetFallDistance();
		return true;
	}

	/** After the local player's own travel: Half-Life's push this tick, or the momentum of one that stopped. */
	public static void push(LocalPlayer player) {
		double seconds = player.level().tickRateManager().millisecondsPerTick() / 1000.0;
		double[] released = HostPush.INSTANCE.takeReleased();
		if (released != null && HostClient.linked()) {
			player.addDeltaMovement(new Vec3(released[0] * seconds, released[1] * seconds, released[2] * seconds));
		}
		double[] shove = HostPush.INSTANCE.takeImpulse();
		if (shove != null && HostClient.linked()) {
			HalfCraft.LOG.info("HalfCraft: Half-Life shoves the player ({} {} {} blocks a second)", String.format("%.2f", shove[0]),
				String.format("%.2f", shove[1]), String.format("%.2f", shove[2]));
			player.addDeltaMovement(new Vec3(shove[0] * seconds, shove[1] * seconds, shove[2] * seconds));
			if (shove[1] > 0.0) {
				player.setOnGround(false);  // off the ground, as Source's shove lifts it
			}
		}
		double[] push = HostClient.linked() ? HostPush.INSTANCE.velocity(System.currentTimeMillis()) : null;
		if ((push != null) != pushed) {
			pushed = push != null;
			if (pushed) {
				HalfCraft.LOG.info("HalfCraft: Half-Life pushes the player ({} {} {} blocks a second)", String.format("%.2f", push[0]),
					String.format("%.2f", push[1]), String.format("%.2f", push[2]));
			} else {
				HalfCraft.LOG.info("HalfCraft: Half-Life stopped pushing the player");
			}
		}
		if (push == null) {
			return;
		}
		boolean grounded = player.onGround();
		player.move(MoverType.SELF, new Vec3(push[0] * seconds, push[1] * seconds, push[2] * seconds));
		// Entity.move takes the local player off its ground on any move without a downward part, but a
		// level push (a conveyor, sideways wind) leaves it standing, as in Source: otherwise it couldn't
		// jump, and Half-Life would lose the conveyor under it. Pushed over an edge, its next travel drops it
		if (grounded && push[1] == 0) {
			player.setOnGround(true);
		}
	}
}

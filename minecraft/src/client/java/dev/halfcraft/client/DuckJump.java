package dev.halfcraft.client;

import dev.halfcraft.client.mixin.CameraEyeAccessor;
import dev.halfcraft.world.HostDuck;
import java.util.function.Consumer;
import net.minecraft.client.Camera;
import net.minecraft.client.Minecraft;
import net.minecraft.client.player.LocalPlayer;
import net.minecraft.world.entity.Pose;
import net.minecraft.world.phys.AABB;

/**
 * Source's duck jump for the local player ({@link HostDuck}): crouching in the air pulls the legs up
 * instead of lowering the head, so a jump reaches the vent a standing jump falls short of, and standing
 * up in the air lets them down again. Where there's no room under the feet for that the player stays
 * crouched until it lands, as Source's does; otherwise crouching and standing up in turn would climb
 * the air. The eyes stay where they were, so the view doesn't jump. A crouch a ceiling forces (an ender
 * pearl that hit one left the head in it) lowers the head instead: pulled up, the legs would go into it.
 */
public final class DuckJump {
	private DuckJump() {
	}

	/**
	 * Player.updatePlayerPose setting the local player's pose.
	 * @param set - the pose change itself
	 */
	public static void setPose(LocalPlayer player, Pose to, Consumer<Pose> set) {
		Pose from = player.getPose();
		if (from == to || !HostDuck.applies(player) || !inTheAir(player)) {
			set.accept(to);
		} else if (from == Pose.STANDING && to == Pose.CROUCHING) {
			set.accept(to);
			if (roomToTuck(player)) {
				shift(player, HostDuck.AIR_SHIFT);
			}
		} else if (from == Pose.CROUCHING && to == Pose.STANDING) {
			if (roomToStand(player)) {
				shift(player, -HostDuck.AIR_SHIFT);
				set.accept(to);
			}
		} else {
			set.accept(to);
		}
	}

	private static boolean inTheAir(LocalPlayer player) {
		return !player.onGround() && !player.isPassenger() && !player.getAbilities().flying && !player.isInWater() && !player.onClimbable();
	}

	/** Room for the crouched player with its legs pulled up: Minecraft's blocks and Half-Life's triangles. */
	private static boolean roomToTuck(LocalPlayer player) {
		AABB tucked = HostDuck.DIMENSIONS.makeBoundingBox(player.getX(), player.getY() + HostDuck.AIR_SHIFT, player.getZ()).deflate(1.0E-7);
		return player.level().noCollision(player, tucked) && HostDuck.tuckRoom(player.getX(), player.getY(), player.getZ(), player.getBbWidth() / 2.0);
	}

	/** Room for the standing player with its feet let down: Minecraft's blocks and Half-Life's triangles. */
	private static boolean roomToStand(LocalPlayer player) {
		AABB standing = player.getDimensions(Pose.STANDING).makeBoundingBox(player.getX(), player.getY() - HostDuck.AIR_SHIFT, player.getZ()).deflate(1.0E-7);
		return player.level().noCollision(player, standing) && HostDuck.legroom(player.getX(), player.getY(), player.getZ(), player.getBbWidth() / 2.0);
	}

	/** Moves the feet by {@code dy}, with no sweep between the two heights, the eyes and the fall left where they were. */
	private static void shift(LocalPlayer player, double dy) {
		player.setPos(player.getX(), player.getY() + dy, player.getZ());
		player.yo += dy;
		player.yOld += dy;
		player.fallDistance = Math.max(0.0, player.fallDistance - dy);
		Camera camera = Minecraft.getInstance().gameRenderer.mainCamera();
		if (camera.entity() == player) {
			CameraEyeAccessor eye = (CameraEyeAccessor) camera;
			eye.halfcraft$setEyeHeight(eye.halfcraft$eyeHeight() - (float) dy);
			eye.halfcraft$setEyeHeightOld(eye.halfcraft$eyeHeightOld() - (float) dy);
		}
	}
}

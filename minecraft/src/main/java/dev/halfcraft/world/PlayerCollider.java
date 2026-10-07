package dev.halfcraft.world;

import java.util.ArrayList;
import java.util.List;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/**
 * Feeds a player's movement through {@link TriCollider} against nearby Half-Life triangles, on both
 * sides: the local player moves with it, and the server's copy of the player falls, lands and
 * stands with it, so the two agree on the ground under it.
 */
public final class PlayerCollider {
	private PlayerCollider() {
	}

	/**
	 * @param move the movement, already collided with Minecraft's blocks
	 * @return the movement collided with Half-Life's triangles as well
	 */
	public static Vec3 collide(Entity player, Vec3 move) {
		AABB box = player.getBoundingBox();
		double step = player.maxUpStep();
		List<HostTri> tris = new ArrayList<>();
		HostCollision.trianglesNear(box.expandTowards(move).inflate(1.0, 1.0 + step, 1.0), tris);
		if (tris.isEmpty()) {
			return move;
		}
		double[] r = TriCollider.resolve(
			tris, (box.minX + box.maxX) * 0.5, box.minY, (box.minZ + box.maxZ) * 0.5, box.getXsize() * 0.5, box.getYsize(), step, player.onGround(),
			move.x, move.y, move.z
		);
		if (r[0] == move.x && r[1] == move.y && r[2] == move.z) {
			return move;
		}
		// The triangle pass (snapping down a slope, pushing out of a wall) can move the player into a
		// Minecraft block placed on the terrain; collide that result with Minecraft blocks again.
		return Entity.collideBoundingBox(player, new Vec3(r[0], r[1], r[2]), box, player.level(), List.of());
	}
}

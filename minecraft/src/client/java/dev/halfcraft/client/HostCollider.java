package dev.halfcraft.client;

import dev.halfcraft.world.HostCollision;
import dev.halfcraft.world.HostTri;
import dev.halfcraft.world.TriCollider;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.world.phys.AABB;

/**
 * Half-Life's triangles around the local player, for putting it back on them (HostClient); its
 * movement collides with them in PlayerCollider.
 */
public final class HostCollider {
	// a floor less than this over the feet is the one they stand on, not one they sank into
	private static final double SUNK_FROM = 0.01;

	private HostCollider() {
	}

	/**
	 * Whether a body {@code height} tall over the feet at (x, y, z) is clear of Half-Life's geometry, a
	 * floor right at the feet aside: the feet haven't sunk into anything.
	 */
	public static boolean bodyClear(double x, double y, double z, double radius, double height) {
		List<HostTri> tris = new ArrayList<>();
		HostCollision.trianglesNear(new AABB(x - radius, y, z - radius, x + radius, y + height, z + radius), tris);
		return TriCollider.clear(tris, x, y + SUNK_FROM, y + height, z, radius);
	}

	/** Highest Half-Life surface at or below {@code maxAbove} over the feet at (x, y, z), or NaN. */
	public static double groundAt(double x, double y, double z, double maxAbove) {
		List<HostTri> tris = new ArrayList<>();
		HostCollision.trianglesNear(new AABB(x - 1, y - 4, z - 1, x + 1, y + maxAbove + 1, z + 1), tris);
		return TriCollider.groundAt(tris, x, y, z, maxAbove);
	}
}

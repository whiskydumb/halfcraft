package dev.halfcraft.world;

import dev.halfcraft.link.HostLink;
import dev.halfcraft.mobs.HostNav;
import java.util.ArrayList;
import java.util.List;
import net.minecraft.world.entity.Avatar;
import net.minecraft.world.entity.EntityAttachment;
import net.minecraft.world.entity.EntityAttachments;
import net.minecraft.world.entity.EntityDimensions;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.AABB;

/**
 * Crouching as Half-Life's duck. In the mirror world Minecraft's crouch takes Source's ducked hull, so
 * the player fits the vents and crawlspaces Half-Life's player ducks through (Minecraft's crouch is 1.5
 * blocks, they're about one), and Half-Life's ceilings keep a crouched player down the way blocks
 * would: Minecraft only asks its blocks whether the player can stand up. Crouching in the air pulls
 * the legs up instead (Source's duck jump, how its higher vents are reached): the client's DuckJump.
 */
public final class HostDuck {
	/** Source's ducked hull and view (VEC_DUCK_HULL_MAX 36, VEC_DUCK_VIEW 28) at 40 units a block. */
	public static final float HEIGHT = 0.9F;
	public static final float EYE_HEIGHT = 0.7F;
	/** Minecraft's crouching pose with Source's height. */
	public static final EntityDimensions DIMENSIONS = EntityDimensions.scalable(0.6F, HEIGHT)
		.withEyeHeight(EYE_HEIGHT)
		.withAttachments(EntityAttachments.builder().attach(EntityAttachment.VEHICLE, Avatar.DEFAULT_VEHICLE_ATTACHMENT));
	/** How far the feet move when the pose changes in the air: Source's standing hull less its ducked one. */
	public static final double AIR_SHIFT = 1.8 - HEIGHT;

	// below this over the feet Half-Life's surfaces are ground the player steps onto (its step height)
	private static final double HEAD_FROM = 0.6;
	// the collider keeps the body this far under a ceiling (TriCollider.pushOutOfWalls)
	private static final double HEAD_GAP = 0.02;

	private HostDuck() {
	}

	/** Whether the player ducks the way Half-Life's does: linked, in the mirror world. */
	public static boolean applies(Player player) {
		return HostLink.active() && HostNav.inMirror(player.level());
	}

	/** Whether Half-Life leaves room for a body {@code height} tall over the feet at (x, y, z). */
	public static boolean headroom(double x, double y, double z, double radius, double height) {
		return clear(x, y + HEAD_FROM, y + height - HEAD_GAP, z, radius);
	}

	/** Whether Half-Life leaves room under the feet at (x, y, z) for legs let down in the air. */
	public static boolean legroom(double x, double y, double z, double radius) {
		return clear(x, y - AIR_SHIFT, y, z, radius);
	}

	/** Whether Half-Life leaves room for a standing player at (x, y, z) to pull its legs up in the air. */
	public static boolean tuckRoom(double x, double y, double z, double radius) {
		return clear(x, y + AIR_SHIFT, y + AIR_SHIFT + HEIGHT - HEAD_GAP, z, radius);
	}

	private static boolean clear(double x, double lo, double hi, double z, double radius) {
		if (hi <= lo) {
			return true;
		}
		List<HostTri> tris = new ArrayList<>();
		HostCollision.trianglesNear(new AABB(x - radius, lo, z - radius, x + radius, hi, z + radius), tris);
		return TriCollider.clear(tris, x, lo, hi, z, radius);
	}
}

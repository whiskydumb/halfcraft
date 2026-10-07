package dev.halfcraft.mixin;

import dev.halfcraft.link.HostLink;
import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.world.HostCollision;
import net.minecraft.core.BlockPos;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.network.ServerGamePacketListenerImpl;
import net.minecraft.world.level.Level;
import net.minecraft.world.phys.AABB;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * A player who says they stand on something the server's copy of them doesn't collide with gets the
 * blocks under their feet sent again ("standing on air - force-sending blocks below", every 10 s). In
 * the mirror world that's Half-Life's ground, which the server's player stands on only as triangles
 * (EntityCollideMixin), not as blocks: there are no blocks to send, and the line kept coming. So is Half-Life holding the player itself (a ladder,
 * a lift, a vehicle: Minecraft's player stands wherever Half-Life's is). Only those cases are skipped:
 * over air with no Half-Life geometry, or under a Minecraft block, the server still checks as vanilla
 * does.
 */
@Mixin(ServerGamePacketListenerImpl.class)
public abstract class PlayerSupportMixin {
	// how far under the feet the server looks for the blocks (forceSendPlayerSupportBlocks)
	@Unique
	private static final double BELOW = 1.0E-5;
	// server thread only
	@Unique
	private static final HostLink.HostState HOST_STATE = new HostLink.HostState();

	@Shadow
	public ServerPlayer player;

	@Inject(method = "forceSendPlayerSupportBlocks", at = @At("HEAD"), cancellable = true)
	private void halfcraft$onHostGround(CallbackInfo ci) {
		Level level = this.player.level();
		if (HostNav.inMirror(level) && (halfcraft$overHostGround(level, this.player.getBoundingBox()) || halfcraft$heldByHost())) {
			ci.cancel();
		}
	}

	// the cells under the box's four corners hold Half-Life geometry and no Minecraft block to stand on
	@Unique
	private static boolean halfcraft$overHostGround(Level level, AABB box) {
		double y = box.minY - BELOW;
		double[][] corners = { { box.minX, box.minZ }, { box.maxX, box.minZ }, { box.minX, box.maxZ }, { box.maxX, box.maxZ } };
		boolean host = false;
		for (double[] corner : corners) {
			BlockPos pos = BlockPos.containing(corner[0], y, corner[1]);
			if (!level.getBlockState(pos).getCollisionShape(level, pos).isEmpty()) {
				return false;
			}
			host |= HostCollision.hasGeometry(pos);
		}
		return host;
	}

	@Unique
	private static boolean halfcraft$heldByHost() {
		return HostLink.active() && HostLink.readHostState(HOST_STATE) && HOST_STATE.takeover();
	}
}

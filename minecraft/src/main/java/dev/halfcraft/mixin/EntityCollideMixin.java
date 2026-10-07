package dev.halfcraft.mixin;

import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.world.HostCollision;
import dev.halfcraft.world.PlayerCollider;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * After vanilla has collided a player's movement with Minecraft blocks, collide it with Half-Life's
 * exact triangles (smooth slopes instead of voxel stair-steps), on both sides: these are the players
 * BlockCollisionsMixin keeps out of Half-Life's voxels. The server's copy of the player, colliding
 * with nothing of Half-Life's, kept falling in place on its ground: never on the ground, and with a
 * speed that a knockback sent back to the player.
 */
@Mixin(Entity.class)
public abstract class EntityCollideMixin {
	@Inject(method = "collide", at = @At("RETURN"), cancellable = true)
	private void halfcraft$smoothHostCollision(Vec3 movement, CallbackInfoReturnable<Vec3> cir) {
		Entity self = (Entity) (Object) this;
		if (!self.noPhysics && HostCollision.usesSmoothCollider(self) && HostNav.inMirror(self.level())) {
			cir.setReturnValue(PlayerCollider.collide(self, cir.getReturnValue()));
		}
	}
}

package dev.halfcraft.mixin;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.world.HostCollision;
import dev.halfcraft.world.HostGround;
import java.util.function.Predicate;
import net.minecraft.core.BlockPos;
import net.minecraft.tags.BlockTags;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.phys.Vec3;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfoReturnable;

/**
 * Random teleports (chorus fruit, endermen) land on Half-Life's floors in the mirror world. Vanilla
 * walks down from the target to the first block entities may teleport onto, and Half-Life's floors
 * are air to it, so it would only ever land on the player's own builds. A Minecraft block standing
 * higher than the Half-Life floor is still vanilla's to land on.
 */
@Mixin(LivingEntity.class)
public abstract class RandomTeleportMixin {
	// how far under the target a Half-Life floor is looked for: vanilla walks down to the world's
	// bottom, this is a deliberate cap (chorus fruit's targets are 8 blocks up or down)
	@Unique
	private static final int MAX_DROP = 64;

	@Shadow
	private boolean checkPositionAndTeleport(double x, double y, double z, boolean showParticles, Predicate<BlockState> avoid, BlockState below, Level level) {
		throw new AssertionError();
	}

	@Inject(method = "randomTeleport(DDDZLjava/util/function/Predicate;)Z", at = @At("HEAD"), cancellable = true)
	private void halfcraft$ontoHostGround(double x, double y, double z, boolean showParticles, Predicate<BlockState> avoid, CallbackInfoReturnable<Boolean> cir) {
		LivingEntity self = (LivingEntity) (Object) this;
		Level level = self.level();
		if (!HostNav.inMirror(level) || !HostCollision.active()) {
			return;
		}
		Vec3 target = level.getWorldBorder().clampVec3ToBound(x, y, z);
		double ground = HostGround.landing(HostNav.CELLS, target.x, target.y, target.z, MAX_DROP);
		if (Double.isNaN(ground)) {
			return;
		}
		BlockPos.MutableBlockPos pos = new BlockPos.MutableBlockPos();
		for (int cell = Mth.floor(target.y) - 1; cell + 1 > ground && cell > level.getMinY(); cell--) {
			if (level.getBlockState(pos.set(Mth.floor(target.x), cell, Mth.floor(target.z))).is(BlockTags.ENTITIES_CAN_TELEPORT_TO)) {
				return;
			}
		}
		BlockState below = level.getBlockState(BlockPos.containing(target.x, ground - 1.0E-3, target.z));
		boolean landed = this.checkPositionAndTeleport(target.x, ground, target.z, showParticles, avoid, below, level);
		// endermen teleport every second or two in the mirror's endless noon: only a player's gets a line
		if (landed && self instanceof Player) {
			HalfCraft.LOG.info("HalfCraft: {} teleported onto Half-Life ground at ({}, {}, {})", self.getName().getString(), String.format("%.2f", target.x),
				String.format("%.2f", ground), String.format("%.2f", target.z));
		}
		cir.setReturnValue(landed);
	}
}

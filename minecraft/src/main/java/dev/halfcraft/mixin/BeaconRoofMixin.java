package dev.halfcraft.mixin;

import com.llamalad7.mixinextras.injector.wrapoperation.Operation;
import com.llamalad7.mixinextras.injector.wrapoperation.WrapOperation;
import dev.halfcraft.HalfCraft;
import dev.halfcraft.mobs.HostNav;
import dev.halfcraft.world.NavGrid;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import net.minecraft.core.BlockPos;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.entity.BeaconBeamOwner;
import net.minecraft.world.level.block.entity.BeaconBlockEntity;
import org.objectweb.asm.Opcodes;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Unique;
import org.spongepowered.asm.mixin.injection.At;

/**
 * A beacon needs open sky over it, and in the mirror world Half-Life's roofs aren't blocks: under one
 * it gets no beam (and gives no effects), as under a Minecraft block. Half-Life's sky itself, solid as
 * its skybox ceiling is, is open sky (NavGrid.roofed). On both sides: the client draws the beam the
 * server's beacon would have.
 */
@Mixin(BeaconBlockEntity.class)
public abstract class BeaconRoofMixin {
	// server thread: beacons under a roof, said once each
	@Unique
	private static final Set<BlockPos> halfcraft$roofed = new HashSet<>();

	@WrapOperation(
		method = "tick",
		at = @At(value = "FIELD", target = "Lnet/minecraft/world/level/block/entity/BeaconBlockEntity;beamSections:Ljava/util/List;", opcode = Opcodes.PUTFIELD)
	)
	private static void halfcraft$noBeamUnderHostRoof(BeaconBlockEntity beacon, List<BeaconBeamOwner.Section> sections, Operation<Void> original) {
		Level level = beacon.getLevel();
		BlockPos pos = beacon.getBlockPos();
		boolean roofed = !sections.isEmpty() && HostNav.inMirror(level) && NavGrid.roofed(HostNav.CELLS, pos.getX(), pos.getY() + 1, pos.getZ());
		if (level != null && !level.isClientSide() && (roofed ? halfcraft$roofed.add(pos.immutable()) : halfcraft$roofed.remove(pos))) {
			HalfCraft.LOG.info("HalfCraft: the beacon at {} {} {} is {} Half-Life's roof", pos.getX(), pos.getY(), pos.getZ(), roofed ? "under" : "out from under");
		}
		original.call(beacon, roofed ? new ArrayList<BeaconBeamOwner.Section>() : sections);
	}
}

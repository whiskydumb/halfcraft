package dev.halfcraft.mobs;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.world.HostCollision;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Leashable;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.TamableAnimal;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.phys.AABB;
import net.minecraft.world.phys.Vec3;

/**
 * The player's pets come along when Half-Life moves the player far in one go: a level transition (the
 * next map is another slot of the mirror world), a load into another map, a long teleport. Vanilla
 * brings a following pet only while its chunk ticks, and the player's old surroundings unload behind it.
 * A pet that follows (tamed by the player, not told to sit) and anything on the player's lead, near
 * where the player left, goes to where the player is now and holds still there until Half-Life's ground
 * streams in (MobHoldMixin). Then a pet takes a free spot around the player the way vanilla's teleport
 * to its owner does (PetTeleportMixin), not the player's own, which would push it off a ledge.
 */
public final class PetsFollow {
	// a move this long between two ticks is Half-Life's, not walking: vanilla's own pet teleport covers
	// shorter ones (a following pet joins an owner more than 12 blocks off)
	static final double JUMP_BLOCKS = 32.0;
	// the pets that were with the player where it left
	static final double ALONG_BLOCKS = 24.0;
	// how long a pet that came along waits for the ground around the player before it stays put
	private static final int SETTLE_TICKS = 20 * 20;
	// where each player was at the start of the last tick
	private static final Map<UUID, Vec3> LAST = new HashMap<>();
	private static final List<Arrival> ARRIVALS = new ArrayList<>();

	/** A pet that came along, until it takes its spot around the player. */
	private record Arrival(TamableAnimal pet, ServerPlayer player, int until) {
	}

	private PetsFollow() {
	}

	public static void init() {
		// at the start of the tick: before the level's own tick snaps a lead the player is now far from
		ServerTickEvents.START_SERVER_TICK.register(PetsFollow::tick);
		ServerLifecycleEvents.SERVER_STOPPING.register(server -> {
			LAST.clear();
			ARRIVALS.clear();
		});
	}

	/** Whether a player's move between two ticks is a jump its pets don't make by themselves. */
	static boolean jumped(Vec3 before, Vec3 now) {
		return before.distanceToSqr(now) > JUMP_BLOCKS * JUMP_BLOCKS;
	}

	private static void tick(MinecraftServer server) {
		for (ServerPlayer player : server.getPlayerList().getPlayers()) {
			Vec3 now = player.position();
			Vec3 before = LAST.put(player.getUUID(), now);
			// a death's respawn leaves the pets where they were, as in vanilla
			if (before != null && player.isAlive() && HostNav.inMirror(player.level()) && jumped(before, now)) {
				bringAlong(player, before, server.getTickCount());
			}
		}
		ARRIVALS.removeIf(arrival -> settled(arrival, server.getTickCount()));
	}

	private static void bringAlong(ServerPlayer player, Vec3 from, int tick) {
		List<Mob> along = player.level().getEntitiesOfClass(Mob.class, new AABB(from, from).inflate(ALONG_BLOCKS), mob -> comesAlong(mob, player));
		if (along.isEmpty()) {
			return;
		}
		for (Mob mob : along) {
			mob.getNavigation().stop();
			mob.teleportTo(player.getX(), player.getY(), player.getZ());
			mob.resetFallDistance();
			if (mob instanceof TamableAnimal pet) {
				ARRIVALS.add(new Arrival(pet, player, tick + SETTLE_TICKS));
			}
		}
		HalfCraft.LOG.info("HalfCraft: {} came along with {} ({} blocks)", along.stream().map(mob -> mob.getName().getString()).collect(Collectors.joining(", ")),
			player.getName().getString(), String.format("%.0f", from.distanceTo(player.position())));
	}

	/** Whether a pet that came along is done: it took its spot, or gave up waiting for the ground. */
	private static boolean settled(Arrival arrival, int tick) {
		TamableAnimal pet = arrival.pet();
		ServerPlayer player = arrival.player();
		if (!pet.isAlive() || player.isRemoved() || pet.level() != player.level() || tick > arrival.until()) {
			return true;
		}
		int x = Mth.floor(player.getX()), y = Mth.floor(player.getY()), z = Mth.floor(player.getZ());
		if (!HostCollision.isKnown(x, y, z) || !HostCollision.isKnown(x, y - 1, z)) {
			return false;
		}
		pet.tryToTeleportToOwner();
		return true;
	}

	/** A pet that follows the player, or anything on the player's lead. */
	static boolean comesAlong(Mob mob, Player player) {
		if (!mob.isAlive() || mob.isPassenger()) {
			return false;
		}
		if (mob instanceof Leashable leashed && leashed.getLeashHolder() == player) {
			return true;
		}
		return mob instanceof TamableAnimal pet && pet.isTame() && pet.isOwnedBy(player) && !pet.isOrderedToSit();
	}
}

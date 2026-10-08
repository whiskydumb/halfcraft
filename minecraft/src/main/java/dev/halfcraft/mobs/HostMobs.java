package dev.halfcraft.mobs;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.combat.HostActorEntity;
import dev.halfcraft.combat.HostCombat;
import dev.halfcraft.link.HostLink;
import dev.halfcraft.world.HostWater;
import java.lang.foreign.MemorySegment;
import java.lang.invoke.VarHandle;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerLifecycleEvents;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.tags.EntityTypeTags;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.TamableAnimal;
import net.minecraft.world.entity.monster.Enemy;
import net.minecraft.world.entity.player.Player;
import org.jspecify.annotations.Nullable;

/**
 * Minecraft's mobs and Half-Life's characters see and fight each other, server side.
 *
 * <p>Every tick the monsters and the player's pets near the player go to Half-Life in the mob table;
 * Half-Life stands an invisible target in for each (halfcraft_mob), which its characters hate or like
 * by their own relationship tables: the Combine and rebels shoot monsters, Half-Life's zombies leave
 * them alone, the player's pets are the player's allies. Animals and villagers aren't listed.
 *
 * <p>What Half-Life does to a stand-in comes back as Minecraft damage from the attacker's own
 * stand-in ({@link HostActorEntity}), so the mob turns on that character. What a mob does to a
 * character's stand-in goes to Half-Life blamed on the mob, not the player. Monsters don't pick
 * fights with Half-Life's characters by themselves; pets defend and help the player as in vanilla.
 */
public final class HostMobs {
	private static final VarHandle INT = JAVA_INT.varHandle();
	// mobs this close to the player are listed (as far as Half-Life sends its own characters)
	private static final double RANGE = 64.0;

	// one "Half-Life hit" line a second per mob at most: an SMG hits once a bullet
	private static final long HURT_LOG_MS = 1000;
	// forgotten all at once past this many mobs (they die or leave; their ids aren't reused)
	private static final int HURT_LOG_MOBS = 256;

	private static final List<Mob> NEARBY = new ArrayList<>();
	// server thread: mob id -> {when its last "Half-Life hit" line was written, hits since}
	private static final Map<Integer, long[]> HURT_LOG = new HashMap<>();
	private static int lastCount;

	private HostMobs() {
	}

	public static void init() {
		ServerTickEvents.END_SERVER_TICK.register(HostMobs::serverTick);
		// leaving the world: its mobs go from the table (Minecraft itself, and the link, may stay)
		ServerLifecycleEvents.SERVER_STOPPING.register(server -> {
			NEARBY.clear();
			HURT_LOG.clear();
			writeTable(NEARBY);
		});
	}

	private static void serverTick(MinecraftServer server) {
		HostNav.endTick();
		HostWater.endTick();
		List<ServerPlayer> players = server.getPlayerList().getPlayers();
		if (!HostLink.active() || players.isEmpty()) {
			NEARBY.clear();
		} else {
			sendMobHits();
			collect(players.getFirst());
		}
		writeTable(NEARBY);
	}

	/** Whether Half-Life has a stand-in for this mob (it's in the mob table), through which Half-Life's hits reach it. Server thread. */
	public static boolean standsIn(Entity entity) {
		int index = entity instanceof Mob mob ? NEARBY.indexOf(mob) : -1;
		return index >= 0 && index < MAX_MOBS;
	}

	/** What Half-Life's characters see a mob as (MOB_* flags); 0: nothing they care about. */
	static int flags(Mob mob) {
		int flags = 0;
		if (mob instanceof Enemy) {
			flags |= MOB_HOSTILE;
		}
		if (mob instanceof TamableAnimal pet && pet.isTame() && pet.getOwner() instanceof Player) {
			flags |= MOB_PET;
		}
		if (flags != 0 && mob.is(EntityTypeTags.UNDEAD)) {
			flags |= MOB_UNDEAD;
		}
		return flags;
	}

	private static void collect(ServerPlayer player) {
		NEARBY.clear();
		NEARBY.addAll(player.level().getEntitiesOfClass(Mob.class, player.getBoundingBox().inflate(RANGE), mob -> mob.isAlive() && flags(mob) != 0));
		NEARBY.sort(Comparator.comparingDouble(mob -> mob.distanceToSqr(player)));
	}

	/** Seqlock write of the mob table (see MobTable in the protocol header). */
	private static void writeTable(List<Mob> mobs) {
		MemorySegment s = HostLink.segment();
		if (s == null) {
			return;
		}
		long b = OFF_MOB_TABLE;
		int seq = s.get(JAVA_INT, b + MT_SEQ);
		INT.setRelease(s, b + MT_SEQ, seq + 1);
		VarHandle.storeStoreFence();
		int count = Math.min(mobs.size(), MAX_MOBS);
		s.set(JAVA_INT, b + MT_COUNT, count);
		for (int i = 0; i < count; i++) {
			Mob mob = mobs.get(i);
			long r = b + MT_RECORDS + i * MOB_RECORD_BYTES;
			s.set(JAVA_INT, r + MR_ID, mob.getId());
			s.set(JAVA_INT, r + MR_FLAGS, flags(mob));
			s.set(JAVA_FLOAT, r + MR_X, (float) mob.getX());
			s.set(JAVA_FLOAT, r + MR_Y, (float) mob.getY());
			s.set(JAVA_FLOAT, r + MR_Z, (float) mob.getZ());
			s.set(JAVA_FLOAT, r + MR_YAW, mob.getYRot());
			s.set(JAVA_FLOAT, r + MR_WIDTH, mob.getBbWidth());
			s.set(JAVA_FLOAT, r + MR_HEIGHT, mob.getBbHeight());
			s.set(JAVA_FLOAT, r + MR_HEALTH, mob.getHealth());
			s.set(JAVA_FLOAT, r + MR_MAX_HEALTH, mob.getMaxHealth());
		}
		INT.setRelease(s, b + MT_SEQ, seq + 2);
		if (count != lastCount && (count % 5 == 0 || count < 5)) {
			HalfCraft.LOG.info("HalfCraft: {} Minecraft mobs near the player listed for Half-Life's characters", count);
		}
		lastCount = count;
	}

	/** Mobs' hits on Half-Life's characters, one event per mob and character, blamed on the mob. */
	private static void sendMobHits() {
		for (HostActorEntity proxy : HostCombat.proxies()) {
			for (HostActorEntity.MobHit hit : proxy.takeMobHits()) {
				HostLink.pushEvent(EV_HIT_ACTOR, proxy.actorId(), hit.damage, (float) hit.pushX, (float) hit.pushZ, hit.pushStrength, hit.flags, hit.weapon,
					hit.attackerId);
				HalfCraft.LOG.info("HalfCraft: {} (mob {}) hit {} for {} (knockback {})", hit.attackerName, hit.attackerId, proxy.getName().getString(), hit.damage,
					hit.pushStrength);
			}
		}
	}

	/**
	 * Half-Life hurt one of Minecraft's mobs through its stand-in (Proto.IN_HURT_MOB). Any thread: the
	 * damage lands on the integrated server.
	 *
	 * @param kind - a Proto.HURT_* value
	 * @param hostDamage - Half-Life's damage (Minecraft divides it by 5, as for the player)
	 * @param attackerId - the attacker's actor id, MOB_ATTACKER_PLAYER for Half-Life's player, 0 for none
	 */
	public static void hurtFromHost(@Nullable MinecraftServer server, int kind, int mobId, float hostDamage, int attackerId) {
		if (server != null && hostDamage > 0.0F) {
			server.execute(() -> hurt(server, kind, mobId, hostDamage, attackerId));
		}
	}

	private static void hurt(MinecraftServer server, int kind, int mobId, float hostDamage, int attackerId) {
		List<ServerPlayer> players = server.getPlayerList().getPlayers();
		if (players.isEmpty()) {
			return;
		}
		ServerPlayer player = players.getFirst();
		ServerLevel level = player.level();
		if (!(level.getEntity(mobId) instanceof Mob mob) || !mob.isAlive()) {
			return;
		}
		DamageSource source;
		String from;
		if (attackerId == MOB_ATTACKER_PLAYER) {
			source = level.damageSources().playerAttack(player);
			from = player.getName().getString();
		} else {
			HostActorEntity attacker = HostCombat.proxy(attackerId);
			// an untyped hit (HURT_OTHER) from a character still comes from that character: the mob fights back
			int as = kind == HURT_OTHER && attacker != null ? HURT_MELEE : kind;
			source = HostCombat.damageSource(level.damageSources(), as, attacker);
			from = attacker != null ? attacker.getName().getString() : "nothing Minecraft knows";
		}
		float damage = hostDamage / HostCombat.HOST_TO_MC_DAMAGE;
		float healthBefore = mob.getHealth();
		boolean hurt = mob.hurtServer(level, source, damage);
		if (hurt) {
			HostCombat.knockBack(mob, source, damage);
		}
		if (HURT_LOG.size() > HURT_LOG_MOBS) {
			HURT_LOG.clear();
		}
		long[] note = HURT_LOG.computeIfAbsent(mobId, id -> new long[2]);
		note[1]++;
		long now = System.currentTimeMillis();
		if (now - note[0] >= HURT_LOG_MS) {
			HalfCraft.LOG.info("HalfCraft: Half-Life hit {} (mob {}) for {} ({} Minecraft) from {}: health {} -> {}{} (hits since the last line: {})",
				mob.getName().getString(), mobId, hostDamage, damage, from, healthBefore, mob.getHealth(), hurt ? "" : " (blocked/immune)", note[1]);
			note[0] = now;
			note[1] = 0;
		}
	}
}

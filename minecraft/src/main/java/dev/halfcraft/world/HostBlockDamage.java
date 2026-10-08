package dev.halfcraft.world;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.mobs.HostMobs;
import dev.halfcraft.mobs.HostNav;
import java.util.List;
import net.minecraft.core.BlockPos;
import net.minecraft.core.SectionPos;
import net.minecraft.core.particles.ParticleTypes;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.resources.Identifier;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.sounds.SoundEvents;
import net.minecraft.tags.TagKey;
import net.minecraft.util.Mth;
import net.minecraft.util.random.WeightedList;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.player.Player;
import net.minecraft.world.level.Explosion;
import net.minecraft.world.level.ExplosionDamageCalculator;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.state.BlockState;
import org.jspecify.annotations.Nullable;

/**
 * Half-Life's blasts and bullets against Minecraft's blocks, on the integrated server.
 *
 * <p>A blast ({@code Proto.IN_BLAST}) goes off here as a silent Minecraft explosion that breaks blocks
 * the way TNT does: blast resistance, TNT's drops and chain reactions, Half-Life's walls in the way
 * (ServerExplosionMixin). Half-Life already hurt the player and the mobs it has stand-ins for, so this
 * one hurts only what Half-Life doesn't know of: animals, villagers, dropped items, boats, paintings.
 *
 * <p>A bullet ({@code Proto.IN_BULLET_HIT}) breaks the block it went into if that's glass, a pane or
 * ice ({@link #BULLET_BREAKABLE}).
 */
public final class HostBlockDamage {
	/** What Half-Life's bullets break: data/halfcraft/tags/block/bullet_breakable.json. */
	public static final TagKey<Block> BULLET_BREAKABLE = TagKey.create(Registries.BLOCK, Identifier.fromNamespaceAndPath(HalfCraft.MOD_ID, "bullet_breakable"));

	/** A grenade's blast (250 units: 6.25 blocks) breaks blocks like TNT (power 4). */
	private static final float POWER_PER_BLOCK = 4.0F / 6.25F;
	/** Half-Life's smallest blasts (a turret's self-destruct) still break glass and dirt. */
	private static final float MIN_POWER = 1.0F;
	/** A charged creeper's: a scripted blast hundreds of units across mustn't level a whole build. */
	private static final float MAX_POWER = 6.0F;
	/** How far an explosion's block rays reach, in powers (see ServerExplosionMixin): the chunks under it must be loaded. */
	private static final float REACH_PER_POWER = 1.8F;

	private static final ExplosionDamageCalculator UNHURT_BY_HOST = new ExplosionDamageCalculator() {
		@Override
		public boolean shouldDamageEntity(Explosion explosion, Entity entity) {
			return !hurtByHost(entity);
		}

		@Override
		public float getKnockbackMultiplier(Entity entity) {
			return hurtByHost(entity) ? 0.0F : 1.0F;
		}
	};

	// server thread: one of Half-Life's blasts is going off, which ServerExplosionMixin mustn't hand back
	private static boolean blasting;

	private HostBlockDamage() {
	}

	/** Whether one of Half-Life's blasts is going off right now (server thread). */
	public static boolean blasting() {
		return blasting;
	}

	/**
	 * A Half-Life blast. Any thread: it goes off on the integrated server.
	 *
	 * @param radius - Half-Life's radius, in blocks
	 */
	public static void blast(@Nullable MinecraftServer server, double x, double y, double z, float radius) {
		if (server != null && radius > 0.0F) {
			server.execute(() -> blastNow(server, x, y, z, radius));
		}
	}

	/** A Half-Life bullet went into the block at pos. Any thread: it breaks on the integrated server. */
	public static void bulletHit(@Nullable MinecraftServer server, BlockPos pos) {
		if (server != null) {
			server.execute(() -> bulletHitNow(server, pos));
		}
	}

	private static void blastNow(MinecraftServer server, double x, double y, double z, float radius) {
		ServerLevel level = mirror(server);
		if (level == null) {
			return;
		}
		float power = Mth.clamp(radius * POWER_PER_BLOCK, MIN_POWER, MAX_POWER);
		int reach = Mth.ceil(power * REACH_PER_POWER);
		int bx = Mth.floor(x), by = Mth.floor(y), bz = Mth.floor(z);
		// a blast out where Minecraft has no chunks loaded would load them on the spot
		if (!chunksLoaded(level, bx, bz, reach)) {
			HalfCraft.LOG.info("HalfCraft: Half-Life's blast at {} {} {} is past Minecraft's loaded chunks; no blocks break", bx, by, bz);
			return;
		}
		blasting = true;
		try {
			level.explode(null, null, UNHURT_BY_HOST, x, y, z, power, false, Level.ExplosionInteraction.TNT, ParticleTypes.EXPLOSION,
				ParticleTypes.EXPLOSION_EMITTER, WeightedList.of(), BuiltInRegistries.SOUND_EVENT.wrapAsHolder(SoundEvents.EMPTY));
		} finally {
			blasting = false;
		}
		HalfCraft.LOG.info("HalfCraft: Half-Life's blast at {} {} {} (radius {} blocks) broke blocks as an explosion of power {}", bx, by, bz,
			String.format("%.1f", radius), String.format("%.1f", power));
	}

	private static void bulletHitNow(MinecraftServer server, BlockPos pos) {
		ServerLevel level = mirror(server);
		if (level == null || !level.isLoaded(pos)) {
			return;
		}
		BlockState state = level.getBlockState(pos);
		if (state.is(BULLET_BREAKABLE) && level.destroyBlock(pos, true)) {
			HalfCraft.LOG.info("HalfCraft: Half-Life's bullet broke {} at {}", BuiltInRegistries.BLOCK.getKey(state.getBlock()), pos.toShortString());
		}
	}

	/** Whether every chunk within reach blocks of the column (x, z) is loaded. */
	private static boolean chunksLoaded(ServerLevel level, int x, int z, int reach) {
		for (int cx = SectionPos.blockToSectionCoord(x - reach); cx <= SectionPos.blockToSectionCoord(x + reach); cx++) {
			for (int cz = SectionPos.blockToSectionCoord(z - reach); cz <= SectionPos.blockToSectionCoord(z + reach); cz++) {
				if (!level.getChunkSource().hasChunk(cx, cz)) {
					return false;
				}
			}
		}
		return true;
	}

	/** Half-Life's world in Minecraft: the player's level, while that's the mirror. */
	private static @Nullable ServerLevel mirror(MinecraftServer server) {
		List<ServerPlayer> players = server.getPlayerList().getPlayers();
		if (players.isEmpty()) {
			return null;
		}
		ServerLevel level = players.getFirst().level();
		return HostNav.inMirror(level) ? level : null;
	}

	/** What Half-Life's own blast reached already: the player (its hurts), and mobs through their stand-ins. */
	private static boolean hurtByHost(Entity entity) {
		return entity instanceof Player || HostMobs.standsIn(entity);
	}
}

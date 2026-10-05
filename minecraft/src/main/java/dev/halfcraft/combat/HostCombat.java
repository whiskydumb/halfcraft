package dev.halfcraft.combat;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.link.Proto;
import dev.halfcraft.link.HostLink;
import dev.halfcraft.mixin.DamageSourceInvoker;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;
import net.fabricmc.fabric.api.event.lifecycle.v1.ServerTickEvents;
import net.fabricmc.fabric.api.object.builder.v1.entity.FabricDefaultAttributeRegistry;
import net.minecraft.core.Registry;
import net.minecraft.core.registries.BuiltInRegistries;
import net.minecraft.core.registries.Registries;
import net.minecraft.network.chat.Component;
import net.minecraft.resources.Identifier;
import net.minecraft.resources.ResourceKey;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.damagesource.DamageSources;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.MobCategory;
import net.minecraft.world.phys.Vec3;
import org.jspecify.annotations.Nullable;

/**
 * Combat between the Minecraft player and Half-Life actors, server side.
 *
 * <p>Every Half-Life actor near the player gets an invisible {@link HostActorEntity} at its exact
 * position. Minecraft weapons hit those like any mob; the resulting damage is sent to Half-Life, which
 * applies it to the real actor (scaled by level) and makes it fight back. Half-Life's hits on the player
 * come back as Minecraft damage from the attacker's stand-in, so armor, shields, knockback, hurt
 * sounds and death all work the Minecraft way.
 */
public final class HostCombat {
	public static final ResourceKey<EntityType<?>> HOST_ACTOR_KEY =
		ResourceKey.create(Registries.ENTITY_TYPE, Identifier.fromNamespaceAndPath(HalfCraft.MOD_ID, "host_actor"));
	public static final EntityType<HostActorEntity> HOST_ACTOR = Registry.register(
		BuiltInRegistries.ENTITY_TYPE,
		HOST_ACTOR_KEY,
		EntityType.Builder.<HostActorEntity>of(HostActorEntity::new, MobCategory.MISC)
			.sized(0.6F, 1.8F)
			.noSave()
			.noSummon()
			.noLootTable()
			.clientTrackingRange(10)
			.updateInterval(1)
			.build(HOST_ACTOR_KEY)
	);

	/** Half-Life damage is divided by this for Minecraft (a 15-damage bandit swing = 3 = 1.5 hearts). */
	public static final float HOST_TO_MC_DAMAGE = 5.0F;

	/** Minecraft's default knockback (LivingEntity.dealDefaultKnockback). */
	private static final double DEFAULT_KNOCKBACK = 0.4;

	private static final Map<Integer, HostActorEntity> PROXIES = new HashMap<>();
	private static final List<HostLink.Actor> ACTORS = new ArrayList<>();
	private static boolean missingTypeLogged;

	private HostCombat() {
	}

	public static void init() {
		FabricDefaultAttributeRegistry.register(HOST_ACTOR, LivingEntity.createLivingAttributes());
		ServerTickEvents.END_SERVER_TICK.register(HostCombat::serverTick);
	}

	public static @Nullable HostActorEntity proxy(int actorId) {
		return PROXIES.get(actorId);
	}

	/** Every Half-Life actor's stand-in right now. Server thread only. */
	public static java.util.Collection<HostActorEntity> proxies() {
		return java.util.Collections.unmodifiableCollection(PROXIES.values());
	}

	private static void serverTick(MinecraftServer server) {
		List<ServerPlayer> players = server.getPlayerList().getPlayers();
		if (!HostLink.active() || players.isEmpty()) {
			removeAll();
			return;
		}
		ServerLevel level = players.getFirst().level();
		for (ServerPlayer player : players) {
			pickUpNearby(player);
		}
		if (HostLink.readActors(ACTORS)) {
			sync(level);
		}
		// Hits land during the tick (melee, sweeps, arrows, fire); send one combined hit per actor.
		for (HostActorEntity proxy : PROXIES.values()) {
			float[] hit = proxy.takeHit();
			if (hit != null && (hit[0] > 0.0F || hit[3] > 0.0F)) {
				HostLink.pushEvent(
					Proto.EV_HIT_ACTOR, proxy.actorId(), hit[0], hit[1], hit[2], hit[3], Float.floatToRawIntBits(hit[4]), Float.floatToRawIntBits(hit[5])
				);
				HalfCraft.LOG.info("HalfCraft: hit {} for {} (knockback {})", proxy.getName().getString(), hit[0], hit[3]);
			}
		}
	}

	private static void sync(ServerLevel level) {
		Map<Integer, HostLink.Actor> live = new HashMap<>();
		for (HostLink.Actor a : ACTORS) {
			if (!a.dead()) {
				live.put(a.actorId(), a);
			}
		}
		for (Iterator<Map.Entry<Integer, HostActorEntity>> it = PROXIES.entrySet().iterator(); it.hasNext(); ) {
			Map.Entry<Integer, HostActorEntity> e = it.next();
			HostActorEntity proxy = e.getValue();
			if (!live.containsKey(e.getKey()) || proxy.isRemoved() || proxy.level() != level) {
				proxy.discard();
				it.remove();
			}
		}
		int before = PROXIES.size();
		for (HostLink.Actor a : live.values()) {
			HostActorEntity proxy = PROXIES.get(a.actorId());
			if (proxy == null) {
				proxy = new HostActorEntity(HOST_ACTOR, level);
				proxy.setActorId(a.actorId());
				proxy.setSize(a.width(), a.height());
				proxy.snapTo(a.x(), a.y(), a.z(), a.yaw(), 0.0F);
				if (!a.name().isEmpty()) {
					proxy.setCustomName(Component.literal(a.name()));
				}
				if (!level.addFreshEntity(proxy)) {
					continue;
				}
				PROXIES.put(a.actorId(), proxy);
				continue;
			}
			proxy.setSize(a.width(), a.height());
			proxy.setPos(a.x(), a.y(), a.z());
			proxy.setYRot(a.yaw());
			proxy.setYHeadRot(a.yaw());
			stepOnTriggers(level, proxy);
		}
		if (PROXIES.size() != before && (PROXIES.size() % 5 == 0 || PROXIES.size() < 5)) {
			HalfCraft.LOG.info("HalfCraft: {} Half-Life actors mirrored as hittable stand-ins", PROXIES.size());
		}
	}

	/**
	 * Half-Life's NPCs press pressure plates and trip tripwires. Their stand-ins are placed, not moved
	 * (no physics), so Minecraft never checks what they step into; do it for those blocks here.
	 */
	private static void stepOnTriggers(ServerLevel level, HostActorEntity proxy) {
		var box = proxy.getBoundingBox().deflate(1.0E-5);
		var from = net.minecraft.core.BlockPos.containing(box.minX, box.minY, box.minZ);
		var to = net.minecraft.core.BlockPos.containing(box.maxX, box.maxY, box.maxZ);
		for (var pos : net.minecraft.core.BlockPos.betweenClosed(from, to)) {
			var state = level.getBlockState(pos);
			if (state.getBlock() instanceof net.minecraft.world.level.block.BasePressurePlateBlock
				|| state.getBlock() instanceof net.minecraft.world.level.block.TripWireBlock) {
				state.entityInside(level, pos, proxy, net.minecraft.world.entity.InsideBlockEffectApplier.NOOP, true);
			}
		}
	}

	/**
	 * Items and stuck arrows on Half-Life ground rest on its collision voxels, which on steep or rough
	 * terrain can sit a little off from where the player (on Half-Life's exact triangles) stands.
	 * Touch them over a slightly bigger area than vanilla's so walking over them picks them up.
	 * playerTouch applies all of Minecraft's own rules (pickup delay, owner, inventory space).
	 */
	private static void pickUpNearby(ServerPlayer player) {
		if (!player.isAlive() || player.isSpectator()) {
			return;
		}
		for (Entity entity : player.level().getEntities(player, player.getBoundingBox().inflate(1.25, 1.0, 1.25))) {
			if (!entity.isRemoved() && (entity instanceof net.minecraft.world.entity.item.ItemEntity
				|| entity instanceof net.minecraft.world.entity.projectile.arrow.AbstractArrow)) {
				entity.playerTouch(player);
			}
		}
	}

	private static void removeAll() {
		if (PROXIES.isEmpty()) {
			return;
		}
		PROXIES.values().forEach(Entity::discard);
		PROXIES.clear();
	}

	/**
	 * Half-Life hit the player. Runs on the server thread. {@code kind} is a Proto.HURT_* value and
	 * {@code hostDamage} is what Half-Life would have taken off the player's health; {@code from} is
	 * where it came from (Proto.IN_HURT_FROM), if Half-Life said.
	 */
	public static void hurtPlayer(ServerPlayer player, int kind, float hostDamage, int attackerActorId, int flags, @Nullable Vec3 from) {
		if (!player.isAlive() || hostDamage <= 0.0F) {
			return;
		}
		ServerLevel level = player.level();
		HostActorEntity attacker = PROXIES.get(attackerActorId);
		DamageSource source = damageSource(level.damageSources(), kind, attacker, from);
		float damage = hostDamage / HOST_TO_MC_DAMAGE;
		float healthBefore = player.getHealth();
		boolean hurt = player.hurtServer(level, source, damage);
		HalfCraft.LOG.info("HalfCraft: Half-Life hit the player for {} ({} Minecraft, {}): health {} -> {}{}", hostDamage, damage,
			source.typeHolder().getRegisteredName(), healthBefore, player.getHealth(), hurt ? "" : " (blocked/immune)");
		if (hurt) {
			knockBack(player, source, damage);
		}
		if (hurt && attacker != null && (flags & Proto.HURT_POWER_ATTACK) != 0 && !player.isBlocking()) {
			// Power attacks shove harder, like a sprint hit does in Minecraft.
			player.knockback(0.5, attacker.getX() - player.getX(), attacker.getZ() - player.getZ(), source, damage);
		}
	}

	/** How Minecraft takes a Half-Life hit of this kind (a Proto.HURT_* value), from the actor's stand-in if any. */
	public static DamageSource damageSource(DamageSources sources, int kind, @Nullable HostActorEntity attacker) {
		return damageSource(sources, kind, attacker, null);
	}

	/** The same, coming from {@code from} when Half-Life said where (see {@link HostHurts}). */
	public static DamageSource damageSource(DamageSources sources, int kind, @Nullable HostActorEntity attacker, @Nullable Vec3 from) {
		HostHurts.Recipe recipe = HostHurts.recipe(kind, attacker != null);
		var type = sources.damageTypes.get(ResourceKey.create(Registries.DAMAGE_TYPE, Identifier.parse(recipe.typeId())));
		if (type.isEmpty()) {
			// HalfCraft's own types come with its data pack; a world that has it turned off has none
			if (!missingTypeLogged) {
				missingTypeLogged = true;
				HalfCraft.LOG.warn("HalfCraft: no damage type {} (is HalfCraft's data pack off?); Half-Life's hits fall back to generic damage", recipe.typeId());
			}
			return sources.generic();
		}
		// with no word from Half-Life on where it came from, it comes from the character who dealt it
		Vec3 at = from != null ? from : attacker != null ? attacker.position() : null;
		return DamageSourceInvoker.halfcraft$create(type.get(), recipe.standInDirect() ? attacker : null, recipe.standInCausing() ? attacker : null,
			recipe.positioned() ? at : null);
	}

	/**
	 * After a hit that landed: pushes the target away from where a hit of HalfCraft's own types came from,
	 * as Minecraft's default knockback would (those types skip it, see {@link HostHurts}). Nothing when
	 * nothing says where it came from (a map's trigger).
	 */
	public static void knockBack(LivingEntity target, DamageSource source, float damage) {
		Vec3 at = source.getSourcePosition();
		if (at != null && HostHurts.isOwnType(source.typeHolder().getRegisteredName())) {
			target.knockback(DEFAULT_KNOCKBACK, at.x - target.getX(), at.z - target.getZ(), source, damage);
		}
	}

	/** The host's armour can fill this much absorption (Half-Life's full suit: 100 / 5). */
	private static final float MAX_HOST_ARMOR = 20.0F;

	/**
	 * The host healed its player (a health kit, a charger) or charged its armour (a suit battery):
	 * the same in Minecraft, scaled like damage. Armour becomes absorption.
	 */
	public static void healPlayer(ServerPlayer player, int kind, float hostPoints) {
		if (!player.isAlive() || hostPoints <= 0.0F) {
			return;
		}
		float amount = hostPoints / HOST_TO_MC_DAMAGE;
		if (kind == Proto.HEAL_ARMOR) {
			var cap = player.getAttribute(net.minecraft.world.entity.ai.attributes.Attributes.MAX_ABSORPTION);
			if (cap != null && cap.getBaseValue() < MAX_HOST_ARMOR) {
				cap.setBaseValue(MAX_HOST_ARMOR);
			}
			player.setAbsorptionAmount(Math.min(MAX_HOST_ARMOR, player.getAbsorptionAmount() + amount));
		} else {
			player.heal(amount);
		}
	}

	/** Id of the Half-Life actor behind a damage source, or 0. */
	public static int attackerActorId(DamageSource source) {
		return source.getEntity() instanceof HostActorEntity proxy ? proxy.actorId() : 0;
	}
}

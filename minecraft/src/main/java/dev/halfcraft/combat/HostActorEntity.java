package dev.halfcraft.combat;

import dev.halfcraft.link.Proto;
import net.minecraft.network.syncher.EntityDataAccessor;
import net.minecraft.network.syncher.EntityDataSerializers;
import net.minecraft.network.syncher.SynchedEntityData;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.sounds.SoundEvent;
import net.minecraft.world.damagesource.DamageSource;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EntityDimensions;
import net.minecraft.world.entity.EntityType;
import net.minecraft.world.entity.HumanoidArm;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.Pose;
import net.minecraft.tags.ItemTags;
import net.minecraft.world.entity.projectile.Projectile;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.item.Items;
import net.minecraft.world.level.Explosion;
import net.minecraft.world.level.Level;
import org.jspecify.annotations.Nullable;

/**
 * An invisible stand-in for one Half-Life actor, so Minecraft's own combat (swords, crits, sweeps,
 * enchantments, attack cooldown, bows, tridents) can target and hit Half-Life NPCs. What it receives is
 * collected into one hit per tick and forwarded to the real actor; its own health never drops.
 */
public class HostActorEntity extends LivingEntity {
	private static final EntityDataAccessor<Integer> FORM_ID = SynchedEntityData.defineId(HostActorEntity.class, EntityDataSerializers.INT);
	private static final EntityDataAccessor<Float> WIDTH = SynchedEntityData.defineId(HostActorEntity.class, EntityDataSerializers.FLOAT);
	private static final EntityDataAccessor<Float> HEIGHT = SynchedEntityData.defineId(HostActorEntity.class, EntityDataSerializers.FLOAT);

	// This tick's hit, flushed to Half-Life by HostCombat after all attacks for the tick have landed
	// (Player.attack adds its sprint/enchantment knockback after hurtServer returns).
	private float pendingDamage;
	private int pendingFlags;
	private int pendingWeapon;
	private double pushX, pushZ;
	private float pushStrength;
	private boolean hitThisTick;
	// This tick's hits by Minecraft's own mobs (a pet, a monster fighting back), one per mob: Half-Life
	// blames each on that mob's stand-in, not on the player (HostMobs sends them)
	private final java.util.Map<Integer, MobHit> mobHits = new java.util.LinkedHashMap<>();

	/** What one of Minecraft's mobs did to this actor in a tick. */
	public static final class MobHit {
		public final int attackerId;
		public final String attackerName;
		public float damage;
		public int flags;
		public int weapon;
		public double pushX, pushZ;
		public float pushStrength;

		MobHit(Mob attacker) {
			this.attackerId = attacker.getId();
			this.attackerName = attacker.getName().getString();
		}
	}

	public HostActorEntity(EntityType<? extends HostActorEntity> type, Level level) {
		super(type, level);
		this.setNoGravity(true);
		this.noPhysics = true;
		this.setInvisible(true);
		this.setSilent(true);
	}

	public int actorId() {
		return this.entityData.get(FORM_ID);
	}

	public void setActorId(int actorId) {
		this.entityData.set(FORM_ID, actorId);
	}

	@Override
	protected void defineSynchedData(SynchedEntityData.Builder builder) {
		super.defineSynchedData(builder);
		builder.define(FORM_ID, 0);
		builder.define(WIDTH, 0.6F);
		builder.define(HEIGHT, 1.8F);
	}

	public void setSize(float width, float height) {
		if (Math.abs(this.entityData.get(WIDTH) - width) > 0.01F || Math.abs(this.entityData.get(HEIGHT) - height) > 0.01F) {
			this.entityData.set(WIDTH, width);
			this.entityData.set(HEIGHT, height);
			this.refreshDimensions();
		}
	}

	@Override
	public void onSyncedDataUpdated(EntityDataAccessor<?> accessor) {
		super.onSyncedDataUpdated(accessor);
		if (WIDTH.equals(accessor) || HEIGHT.equals(accessor)) {
			this.refreshDimensions();
		}
	}

	@Override
	protected EntityDimensions getDefaultDimensions(Pose pose) {
		return EntityDimensions.scalable(this.entityData.get(WIDTH), this.entityData.get(HEIGHT));
	}

	@Override
	protected void actuallyHurt(ServerLevel level, DamageSource source, float dmg) {
		// Minecraft has applied everything (crit, sharpness, strength, cooldown, invulnerability
		// frames). Hand the result to Half-Life instead of lowering our own health.
		if (this.isInvulnerableTo(level, source) || dmg <= 0.0F) {
			return;
		}
		if (source.getEntity() instanceof Mob attacker) {
			MobHit hit = this.mobHits.computeIfAbsent(attacker.getId(), id -> new MobHit(attacker));
			hit.damage += dmg;
			hit.flags |= hitFlags(source);
			hit.weapon = weaponClass(source);
			this.getCombatTracker().recordDamage(source, dmg);
			return;
		}
		this.pendingDamage += dmg;
		this.pendingFlags |= hitFlags(source);
		this.pendingWeapon = weaponClass(source);
		this.hitThisTick = true;
		this.getCombatTracker().recordDamage(source, dmg);
	}

	private static int hitFlags(DamageSource source) {
		int flags = 0;
		if (source.getDirectEntity() instanceof Projectile) {
			flags |= Proto.HIT_PROJECTILE;
		}
		if (source.is(net.minecraft.tags.DamageTypeTags.IS_FIRE)) {
			flags |= Proto.HIT_FIRE;
		}
		return flags;
	}

	@Override
	public void knockback(double power, double xd, double zd, DamageSource source, float damage, boolean comesFromEffect) {
		// Half-Life owns this actor's position. Remember the strongest push for Half-Life's stagger:
		// Minecraft pushes towards -(xd, zd).
		double len = Math.sqrt(xd * xd + zd * zd);
		if (source.getEntity() instanceof Mob attacker) {
			MobHit hit = this.mobHits.computeIfAbsent(attacker.getId(), id -> new MobHit(attacker));
			if (len > 1e-6 && power > hit.pushStrength) {
				hit.pushStrength = (float) power;
				hit.pushX = -xd / len;
				hit.pushZ = -zd / len;
			}
			return;
		}
		if (len > 1e-6 && power > this.pushStrength) {
			this.pushStrength = (float) power;
			this.pushX = -xd / len;
			this.pushZ = -zd / len;
		}
		this.hitThisTick = true;
	}

	/** Player.crit() was called on us this tick. */
	public void markCritical() {
		this.pendingFlags |= Proto.HIT_CRITICAL;
	}

	/** Which kind of Half-Life weapon impact this hit should look and sound like. */
	private static int weaponClass(DamageSource source) {
		if (source.getDirectEntity() instanceof net.minecraft.world.entity.projectile.arrow.ThrownTrident) {
			return Proto.WEAPON_PIERCE;
		}
		if (source.getDirectEntity() instanceof Projectile) {
			return Proto.WEAPON_ARROW;
		}
		ItemStack weapon = source.getWeaponItem();
		if (weapon == null && source.getEntity() instanceof LivingEntity attacker) {
			weapon = attacker.getMainHandItem();
		}
		if (weapon == null || weapon.isEmpty()) {
			return Proto.WEAPON_UNARMED;
		}
		if (weapon.is(ItemTags.SWORDS)) {
			return Proto.WEAPON_BLADE;
		}
		if (weapon.is(ItemTags.AXES)) {
			return Proto.WEAPON_AXE;
		}
		if (weapon.is(Items.TRIDENT)) {
			return Proto.WEAPON_PIERCE;
		}
		return Proto.WEAPON_BLUNT;
	}

	/** Returns this tick's hit (damage, flags, push, weapon) and clears it; null if nothing hit us. */
	public float[] takeHit() {
		if (!this.hitThisTick) {
			return null;
		}
		float[] hit = { this.pendingDamage, (float) this.pushX, (float) this.pushZ, this.pushStrength, Float.intBitsToFloat(this.pendingFlags),
			Float.intBitsToFloat(this.pendingWeapon) };
		this.pendingDamage = 0.0F;
		this.pendingFlags = 0;
		this.pushX = this.pushZ = 0.0;
		this.pushStrength = 0.0F;
		this.hitThisTick = false;
		return hit;
	}

	/** This tick's hits by Minecraft's mobs, one per mob, and clears them. */
	public java.util.List<MobHit> takeMobHits() {
		if (this.mobHits.isEmpty()) {
			return java.util.List.of();
		}
		java.util.List<MobHit> hits = new java.util.ArrayList<>(this.mobHits.values());
		this.mobHits.clear();
		return hits;
	}

	@Override
	public boolean ignoreExplosion(Explosion explosion) {
		// Half-Life's own blast hurts the actor (ServerExplosionMixin sends it); hit here too, it would take
		// the explosion twice, once typed as whatever the player holds. Wind bursts only push, and only here.
		return !HostBlasts.isWindBurst(explosion);
	}

	@Override
	public double getVisibilityPercent(ServerLevel level, @Nullable Entity lookingEntity) {
		// invisible only to the eye (Half-Life draws the actor): pets and mobs target it at full range
		return 1.0;
	}

	@Override
	public void tick() {
		// Position and rotation come from Half-Life (HostCombat); keep hurt timers and fire ticking.
		this.baseTick();
		this.setHealth(this.getMaxHealth());
	}

	@Override
	public boolean isPushable() {
		return false;
	}

	@Override
	protected void doPush(Entity entity) {
	}

	@Override
	public boolean canBeCollidedWith(@Nullable Entity other) {
		return false;
	}

	@Override
	public boolean shouldShowName() {
		return false;
	}

	@Override
	public boolean shouldBeSaved() {
		return false;
	}

	@Override
	protected @Nullable SoundEvent getHurtSound(DamageSource source) {
		return null; // Half-Life plays the NPC's own pain sounds
	}

	@Override
	protected @Nullable SoundEvent getDeathSound() {
		return null;
	}

	@Override
	public HumanoidArm getMainArm() {
		return HumanoidArm.RIGHT;
	}
}

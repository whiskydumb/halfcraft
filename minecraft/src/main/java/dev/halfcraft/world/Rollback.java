package dev.halfcraft.world;

import dev.halfcraft.HalfCraft;
import it.unimi.dsi.fastutil.longs.Long2ObjectOpenHashMap;
import it.unimi.dsi.fastutil.longs.LongOpenHashSet;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.function.Predicate;
import java.util.stream.Stream;
import net.minecraft.core.BlockPos;
import net.minecraft.core.HolderGetter;
import net.minecraft.core.SectionPos;
import net.minecraft.core.registries.Registries;
import net.minecraft.nbt.CompoundTag;
import net.minecraft.nbt.ListTag;
import net.minecraft.nbt.NbtAccounter;
import net.minecraft.nbt.NbtIo;
import net.minecraft.nbt.NbtUtils;
import net.minecraft.nbt.Tag;
import net.minecraft.server.MinecraftServer;
import net.minecraft.server.level.ServerLevel;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.util.ProblemReporter;
import net.minecraft.world.ItemStackWithSlot;
import net.minecraft.world.effect.MobEffectInstance;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.EquipmentSlot;
import net.minecraft.world.entity.ExperienceOrb;
import net.minecraft.world.entity.Mob;
import net.minecraft.world.entity.TamableAnimal;
import net.minecraft.world.entity.boss.enderdragon.EndCrystal;
import net.minecraft.world.entity.decoration.ArmorStand;
import net.minecraft.world.entity.decoration.BlockAttachedEntity;
import net.minecraft.world.entity.item.FallingBlockEntity;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.item.PrimedTnt;
import net.minecraft.world.entity.projectile.arrow.AbstractArrow;
import net.minecraft.world.entity.vehicle.VehicleEntity;
import net.minecraft.world.item.ItemStack;
import net.minecraft.world.level.ChunkPos;
import net.minecraft.world.level.Level;
import net.minecraft.world.level.block.Block;
import net.minecraft.world.level.block.entity.BlockEntity;
import net.minecraft.world.level.block.state.BlockState;
import net.minecraft.world.level.chunk.LevelChunk;
import net.minecraft.world.level.storage.LevelResource;
import net.minecraft.world.level.storage.TagValueInput;
import net.minecraft.world.level.storage.TagValueOutput;
import net.minecraft.world.level.storage.ValueInput;
import org.jspecify.annotations.Nullable;

/**
 * Minecraft's world and player rolled back with Half-Life's saves.
 *
 * <p>Every block of the mirror world that ever changes is tracked: its state before the first
 * change, and its latest. Half-Life saving makes a checkpoint (the tracked blocks and the player as
 * they are now, in a file named by an id the save itself carries); Half-Life loading a save
 * restores it: tracked blocks go back to the checkpoint's state, or to how they were before
 * anything changed them if they changed only after it. Any save, in any order. Dropped items,
 * arrows, lit TNT and falling blocks go too; the player gets back inventory, armour, health,
 * hunger, experience and effects (not the position: Half-Life puts the player where the save has
 * them).
 *
 * <p>A new game from Half-Life's menu starts a playthrough: each map it enters for the first time
 * (the new game's own, then each one a level transition brings up) goes back to how it was before
 * anything changed it, and what the playthrough before left there (things lying about, boats, frames,
 * armour stands, mobs other than pets) goes: in each chunk the first time its things load in the
 * playthrough (see {@link Playthrough}). The playthrough is kept with the block log and with every
 * checkpoint, so a save brings back its own. A world without one (older than this, or a checkpoint from
 * before it) clears nothing until the next new game.
 *
 * <p>Server thread only.
 */
public final class Rollback {
	private static final int MAX_CHECKPOINTS = 256;
	// A map reaches at most this many chunks north and south of its slot's middle (a source map spans +-410 blocks).
	private static final int SLOT_CHUNKS = 32;
	// Back the way it was, with no neighbour updates, drops, block entity side effects (a chest
	// spilling its contents) or placement reactions (TNT lighting).
	private static final int RESTORE_FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_SKIP_ALL_SIDEEFFECTS;
	private static final EquipmentSlot[] WORN = { EquipmentSlot.HEAD, EquipmentSlot.CHEST, EquipmentSlot.LEGS, EquipmentSlot.FEET, EquipmentSlot.OFFHAND };

	/** A block as it was: its state and its block entity's data (null: none, or not captured). */
	private record Snapshot(BlockState state, @Nullable CompoundTag blockEntity) {
	}

	private static final Long2ObjectOpenHashMap<Snapshot> ORIGINALS = new Long2ObjectOpenHashMap<>();
	private static final Long2ObjectOpenHashMap<Snapshot> LATEST = new Long2ObjectOpenHashMap<>();

	// How often the things a sweep removed as chunks loaded are logged at most.
	private static final long SWEEP_LOG_MS = 5000;
	// The current playthrough; null: none (nothing gets cleared).
	private static @Nullable Playthrough playthrough;
	private static int sweptSinceLog;
	private static long sweepLoggedMs;

	private Rollback() {
	}

	// ---- tracking --------------------------------------------------------------------------

	/** The world opened: picks up what earlier sessions tracked. */
	public static void load(MinecraftServer opened) {
		ORIGINALS.clear();
		LATEST.clear();
		playthrough = null;
		Path file = dir(opened).resolve("blocks.nbt");
		if (!Files.exists(file)) {
			return;
		}
		try {
			CompoundTag root = NbtIo.readCompressed(file, NbtAccounter.unlimitedHeap());
			HolderGetter<Block> blocks = opened.overworld().registryAccess().lookupOrThrow(Registries.BLOCK);
			readSnapshots(root.getListOrEmpty("originals"), blocks, ORIGINALS);
			readSnapshots(root.getListOrEmpty("latest"), blocks, LATEST);
			playthrough = readPlaythrough(root);
			HalfCraft.LOG.info("HalfCraft: rollback tracks {} blocks; {}", ORIGINALS.size(), describe());
		} catch (IOException | RuntimeException e) {
			HalfCraft.LOG.warn("HalfCraft: couldn't read the rollback block log {}", file, e);
		}
	}

	/** The world is closing: what's loaded is captured and everything written down. */
	public static void unload(MinecraftServer closing) {
		ServerLevel level = closing.overworld();
		for (long key : ORIGINALS.keySet()) {
			LevelChunk chunk = loadedChunk(level, key);
			if (chunk != null) {
				LATEST.put(key, capture(chunk, BlockPos.of(key)));
			}
		}
		writeBlockLog(closing);
		ORIGINALS.clear();
		LATEST.clear();
		playthrough = null;
	}

	/** LevelChunk.setBlockState, before: the first change to a block remembers how it was. */
	public static void beforeChange(LevelChunk chunk, BlockPos pos) {
		if (tracks(chunk)) {
			long key = pos.asLong();
			if (!ORIGINALS.containsKey(key)) {
				ORIGINALS.put(key, capture(chunk, pos));
			}
		}
	}

	/** LevelChunk.setBlockState, after: the block's latest state (its block entity is read later). */
	public static void afterChange(LevelChunk chunk, BlockPos pos) {
		if (tracks(chunk)) {
			LATEST.put(pos.asLong(), new Snapshot(chunk.getBlockState(pos), null));
		}
	}

	/** A chunk unloads: its tracked blocks' block entities as they are now, before they're out of reach. */
	public static void chunkUnloading(ServerLevel level, LevelChunk chunk) {
		if (level != level.getServer().overworld() || ORIGINALS.isEmpty()) {
			return;
		}
		int cx = chunk.getPos().x(), cz = chunk.getPos().z();
		for (long key : ORIGINALS.keySet()) {
			if (BlockPos.getX(key) >> 4 == cx && BlockPos.getZ(key) >> 4 == cz) {
				LATEST.put(key, capture(chunk, BlockPos.of(key)));
			}
		}
	}

	private static boolean tracks(LevelChunk chunk) {
		Level level = chunk.getLevel();
		return level instanceof ServerLevel serverLevel && serverLevel.dimension() == Level.OVERWORLD && serverLevel.getServer().isSameThread();
	}

	private static Snapshot capture(LevelChunk chunk, BlockPos pos) {
		BlockEntity blockEntity = chunk.getBlockEntity(pos);
		return new Snapshot(chunk.getBlockState(pos), blockEntity != null ? blockEntity.saveWithFullMetadata(chunk.getLevel().registryAccess()) : null);
	}

	private static @Nullable LevelChunk loadedChunk(ServerLevel level, long key) {
		return level.getChunkSource().getChunkNow(BlockPos.getX(key) >> 4, BlockPos.getZ(key) >> 4);
	}

	// ---- checkpoints -------------------------------------------------------------------------

	/** Half-Life saved: the tracked blocks and the player as they are now, under its id. */
	public static void checkpoint(MinecraftServer from, long id, @Nullable ServerPlayer player) {
		ServerLevel level = from.overworld();
		// the gravity gun's blocks are where they were taken from as far as a save goes: its cubes aren't saved
		Long2ObjectOpenHashMap<BlockState> held = new Long2ObjectOpenHashMap<>();
		HeldBlocks.forEachOrigin((pos, state) -> held.put(pos.asLong(), state));
		ListTag blocks = new ListTag();
		for (long key : ORIGINALS.keySet()) {
			LevelChunk chunk = loadedChunk(level, key);
			Snapshot now = chunk != null ? capture(chunk, BlockPos.of(key)) : LATEST.getOrDefault(key, ORIGINALS.get(key));
			LATEST.put(key, now);
			BlockState taken = held.get(key);
			blocks.add(writeSnapshot(key, taken != null ? new Snapshot(taken, null) : now));
		}
		CompoundTag root = new CompoundTag();
		root.put("blocks", blocks);
		writePlaythrough(root);
		if (player != null) {
			root.put("player", savePlayer(player));
		}
		Path file = checkpointFile(from, id);
		try {
			Files.createDirectories(file.getParent());
			NbtIo.writeCompressed(root, file);
		} catch (IOException e) {
			HalfCraft.LOG.warn("HalfCraft: couldn't write checkpoint {}", file, e);
			return;
		}
		writeBlockLog(from);
		prune(file.getParent());
		HalfCraft.LOG.info("HalfCraft: checkpoint {} ({} blocks{})", Long.toHexString(id), blocks.size(), player != null ? ", the player" : "");
	}

	/** Half-Life loaded a save: the world and the player back to its checkpoint. */
	public static void restore(MinecraftServer from, long id, @Nullable ServerPlayer player) {
		Path file = checkpointFile(from, id);
		if (!Files.exists(file)) {
			HalfCraft.LOG.warn("HalfCraft: no checkpoint {} (Minecraft wasn't running when Half-Life saved?); nothing rolled back", Long.toHexString(id));
			return;
		}
		CompoundTag root;
		try {
			root = NbtIo.readCompressed(file, NbtAccounter.unlimitedHeap());
		} catch (IOException e) {
			HalfCraft.LOG.warn("HalfCraft: couldn't read checkpoint {}", file, e);
			return;
		}
		ServerLevel level = from.overworld();
		HeldBlocks.forget();  // the gravity gun's cubes went with the level; the checkpoint has their blocks
		Long2ObjectOpenHashMap<Snapshot> target = new Long2ObjectOpenHashMap<>();
		readSnapshots(root.getListOrEmpty("blocks"), level.registryAccess().lookupOrThrow(Registries.BLOCK), target);

		LongOpenHashSet keys = new LongOpenHashSet(ORIGINALS.keySet());
		keys.addAll(target.keySet());
		int changed = 0;
		for (long key : keys) {
			Snapshot want = target.containsKey(key) ? target.get(key) : ORIGINALS.get(key);
			if (want != null && putBack(level, key, want)) {
				changed++;
			}
		}

		// What was flying or lying about since is gone (and whatever a removed chest held).
		int removed = discard(level, Rollback::isTransient);
		playthrough = readPlaythrough(root);

		if (player != null && root.contains("player")) {
			loadPlayer(player, root.getCompoundOrEmpty("player"));
		}
		HalfCraft.LOG.info("HalfCraft: rolled back to checkpoint {}: {} blocks, {} things removed{}; {}", Long.toHexString(id), changed, removed,
			player != null ? ", the player" : "", describe());
	}

	/** The block at key back to want, unless it's so already (nothing to load, and its chunk can stay where it is). */
	private static boolean putBack(ServerLevel level, long key, Snapshot want) {
		Snapshot have = LATEST.get(key);
		if (have != null && have.state() == want.state() && want.blockEntity() == null) {
			return false;
		}
		BlockPos pos = BlockPos.of(key);
		level.setBlock(pos, want.state(), RESTORE_FLAGS);
		if (want.blockEntity() != null) {
			BlockEntity blockEntity = level.getBlockEntity(pos);
			if (blockEntity != null) {
				blockEntity.loadWithComponents(TagValueInput.create(ProblemReporter.DISCARDING, level.registryAccess(), want.blockEntity()));
				blockEntity.setChanged();
				level.sendBlockUpdated(pos, want.state(), want.state(), Block.UPDATE_CLIENTS);
			}
		}
		LATEST.put(key, want);
		return true;
	}

	/** Removes the level's loaded entities that match. @return how many went */
	private static int discard(ServerLevel level, Predicate<Entity> which) {
		List<Entity> gone = new ArrayList<>();
		for (Entity entity : level.getAllEntities()) {
			if (which.test(entity)) {
				gone.add(entity);
			}
		}
		gone.forEach(Entity::discard);
		return gone.size();
	}

	/** Something flying or lying about: dropped items, arrows, lit TNT, falling blocks, experience. */
	private static boolean isTransient(Entity entity) {
		return entity instanceof ItemEntity || entity instanceof AbstractArrow || entity instanceof PrimedTnt || entity instanceof FallingBlockEntity
			|| entity instanceof ExperienceOrb;
	}

	// ---- playthroughs --------------------------------------------------------------------------

	/**
	 * Half-Life's player entered a map afresh (Proto.IN_MAP_ENTERED): a new game from the menu starts a
	 * playthrough, and the first time a playthrough enters a map, the map's tracked blocks go back to how
	 * they were before anything changed them, and what the playthrough before left there goes: now in
	 * the chunks whose things are loaded, the rest as they load ({@link #sweepsOnLoad}).
	 *
	 * @param newGame - a new game from Half-Life's menu; otherwise a level transition
	 * @param slot - the map's slot
	 * @param west - the slot's west edge (Minecraft x)
	 * @param east - its east edge (exclusive)
	 * @param player - healed and fed for a new game, keeping everything they carry
	 */
	public static void mapEntered(MinecraftServer server, boolean newGame, int slot, int west, int east, @Nullable ServerPlayer player) {
		if (newGame) {
			playthrough = new Playthrough();
			if (player != null) {
				freshStart(player);
			}
		} else if (playthrough == null || playthrough.entered(slot)) {
			return;
		}
		Playthrough current = playthrough;
		current.enter(slot, west, east);
		ServerLevel level = server.overworld();
		int changed = 0;
		for (long key : ORIGINALS.keySet().toLongArray()) {
			int x = BlockPos.getX(key);
			if (x >= west && x < east && putBack(level, key, ORIGINALS.get(key))) {
				changed++;
			}
		}
		// the chunks whose things are in the world already: those that aren't sweep as they load
		LongOpenHashSet loaded = new LongOpenHashSet();
		for (int cx = SectionPos.blockToSectionCoord(west); cx < SectionPos.blockToSectionCoord(east); cx++) {
			for (int cz = -SLOT_CHUNKS; cz <= SLOT_CHUNKS; cz++) {
				if (level.areEntitiesLoaded(ChunkPos.pack(cx, cz))) {
					current.markSwept(cx, cz);
					loaded.add(ChunkPos.pack(cx, cz));
				}
			}
		}
		int removed = discard(level, entity -> loaded.contains(ChunkPos.pack(entity.blockPosition())) && isLeftBehind(entity));
		writeBlockLog(server);
		HalfCraft.LOG.info("HalfCraft: {} map slot {}: {} blocks back as they were before anything was built, {} things left from before removed; {}",
			newGame ? "new game in" : "first time in", slot, changed, removed, describe());
	}

	/**
	 * A chunk's things are loading (PersistentEntitySectionManager.processPendingLoads): whether they're
	 * from before the playthrough, in a map it entered, loading for the first time in it. Those that are
	 * {@link #isLeftBehind} go then. Counts the chunk as swept.
	 */
	public static boolean sweepsOnLoad(ServerLevel level, ChunkPos pos) {
		Playthrough current = playthrough;
		return current != null && level.dimension() == Level.OVERWORLD && current.sweep(pos.x(), pos.z());
	}

	/** {@link #sweepsOnLoad} took this many things out of a chunk as it loaded. */
	public static void sweptOnLoad(int removed) {
		sweptSinceLog += removed;
		long now = System.currentTimeMillis();
		if (sweptSinceLog > 0 && now - sweepLoggedMs >= SWEEP_LOG_MS) {
			HalfCraft.LOG.info("HalfCraft: {} things left from before the playthrough removed as their chunks loaded", sweptSinceLog);
			sweptSinceLog = 0;
			sweepLoggedMs = now;
		}
	}

	/**
	 * What a cleared map loses besides its blocks: what lies about, what was built of entities (frames,
	 * paintings, armour stands, empty boats and minecarts, end crystals) and mobs. Pets stay.
	 */
	public static boolean isLeftBehind(Entity entity) {
		return isTransient(entity) || entity instanceof BlockAttachedEntity || entity instanceof ArmorStand || entity instanceof EndCrystal
			|| entity instanceof VehicleEntity && !entity.isVehicle() || entity instanceof Mob && !(entity instanceof TamableAnimal pet && pet.isTame());
	}

	/** A new game: the player comes in healthy and fed, with everything they carry. */
	private static void freshStart(ServerPlayer player) {
		if (!player.isAlive()) {
			return;
		}
		player.setHealth(player.getMaxHealth());
		player.getFoodData().setFoodLevel(20);
		player.getFoodData().setSaturation(5.0F);
		player.clearFire();
		player.setAirSupply(player.getMaxAirSupply());
	}

	private static @Nullable Playthrough readPlaythrough(CompoundTag root) {
		return root.getIntArray("visited")
			.map(slots -> Playthrough.of(slots, root.getIntArray("map_edges").orElse(new int[0]), root.getLongArray("swept").orElse(new long[0])))
			.orElse(null);
	}

	private static void writePlaythrough(CompoundTag root) {
		Playthrough current = playthrough;
		if (current != null) {
			root.putIntArray("visited", current.slots());
			root.putIntArray("map_edges", current.edges());
			root.putLongArray("swept", current.swept());
		}
	}

	private static String describe() {
		Playthrough current = playthrough;
		return current == null ? "no playthrough" : current.toString();
	}

	// ---- the player ----------------------------------------------------------------------------

	private static CompoundTag savePlayer(ServerPlayer player) {
		TagValueOutput out = TagValueOutput.createWithContext(ProblemReporter.DISCARDING, player.registryAccess());
		// Half-Life's weapons are Half-Life's to save: its own load brings them back. Out of the
		// armour and the offhand too, where one can sit until the next tick puts it back
		var standIns = dev.halfcraft.weapon.HostWeapons.takeOut(player.getInventory());
		player.getInventory().save(out.list("inventory", ItemStackWithSlot.CODEC));
		for (EquipmentSlot slot : WORN) {
			out.store(slot.getName(), ItemStack.OPTIONAL_CODEC, player.getItemBySlot(slot));
		}
		dev.halfcraft.weapon.HostWeapons.putBack(player.getInventory(), standIns);
		out.putFloat("health", player.getHealth());
		out.putFloat("absorption", player.getAbsorptionAmount());
		player.getFoodData().addAdditionalSaveData(out.child("food"));
		out.putInt("xp_level", player.experienceLevel);
		out.putInt("xp_total", player.totalExperience);
		out.putFloat("xp_progress", player.experienceProgress);
		out.store("effects", MobEffectInstance.CODEC.listOf(), List.copyOf(player.getActiveEffects()));
		return out.buildResult();
	}

	private static void loadPlayer(ServerPlayer player, CompoundTag tag) {
		ValueInput in = TagValueInput.create(ProblemReporter.DISCARDING, player.registryAccess(), tag);
		// Half-Life's weapons stay as they are (an older checkpoint may still hold some: those go)
		var standIns = dev.halfcraft.weapon.HostWeapons.takeOut(player.getInventory());
		player.getInventory().load(in.listOrEmpty("inventory", ItemStackWithSlot.CODEC));
		for (EquipmentSlot slot : WORN) {
			player.setItemSlot(slot, in.read(slot.getName(), ItemStack.OPTIONAL_CODEC).orElse(ItemStack.EMPTY));
		}
		dev.halfcraft.weapon.HostWeapons.takeOut(player.getInventory());
		dev.halfcraft.weapon.HostWeapons.putBack(player.getInventory(), standIns);
		player.setHealth(in.getFloatOr("health", player.getMaxHealth()));
		player.setAbsorptionAmount(in.getFloatOr("absorption", 0.0F));
		player.getFoodData().readAdditionalSaveData(in.childOrEmpty("food"));
		player.experienceLevel = in.getIntOr("xp_level", 0);
		player.totalExperience = in.getIntOr("xp_total", 0);
		player.experienceProgress = in.getFloatOr("xp_progress", 0.0F);
		player.removeAllEffects();
		in.read("effects", MobEffectInstance.CODEC.listOf()).orElse(List.of()).forEach(player::addEffect);
		player.inventoryMenu.broadcastChanges();
	}

	// ---- files ---------------------------------------------------------------------------------

	private static Path dir(MinecraftServer of) {
		return of.getWorldPath(LevelResource.ROOT).resolve("halfcraft");
	}

	private static Path checkpointFile(MinecraftServer of, long id) {
		return dir(of).resolve("checkpoints").resolve(Long.toHexString(id) + ".nbt");
	}

	private static void writeBlockLog(MinecraftServer of) {
		CompoundTag root = new CompoundTag();
		root.put("originals", writeSnapshots(ORIGINALS));
		root.put("latest", writeSnapshots(LATEST));
		writePlaythrough(root);
		Path file = dir(of).resolve("blocks.nbt");
		try {
			Files.createDirectories(file.getParent());
			NbtIo.writeCompressed(root, file);
		} catch (IOException e) {
			HalfCraft.LOG.warn("HalfCraft: couldn't write the rollback block log {}", file, e);
		}
	}

	/** The oldest checkpoints go once there are too many (saves that old roll nothing back). */
	private static void prune(Path checkpoints) {
		try (Stream<Path> files = Files.list(checkpoints)) {
			List<Path> all = files.sorted(Comparator.comparingLong(Rollback::modified).reversed()).toList();
			for (Path old : all.subList(Math.min(all.size(), MAX_CHECKPOINTS), all.size())) {
				Files.deleteIfExists(old);
			}
		} catch (IOException e) {
			HalfCraft.LOG.warn("HalfCraft: couldn't prune old checkpoints", e);
		}
	}

	private static long modified(Path file) {
		try {
			return Files.getLastModifiedTime(file).toMillis();
		} catch (IOException e) {
			return 0L;
		}
	}

	private static ListTag writeSnapshots(Long2ObjectOpenHashMap<Snapshot> snapshots) {
		ListTag list = new ListTag();
		snapshots.long2ObjectEntrySet().forEach(e -> list.add(writeSnapshot(e.getLongKey(), e.getValue())));
		return list;
	}

	private static CompoundTag writeSnapshot(long key, Snapshot snapshot) {
		CompoundTag tag = new CompoundTag();
		tag.putLong("pos", key);
		tag.put("state", NbtUtils.writeBlockState(snapshot.state()));
		if (snapshot.blockEntity() != null) {
			tag.put("block_entity", snapshot.blockEntity());
		}
		return tag;
	}

	private static void readSnapshots(ListTag list, HolderGetter<Block> blocks, Long2ObjectOpenHashMap<Snapshot> out) {
		for (Tag element : list) {
			if (element instanceof CompoundTag tag) {
				BlockState state = NbtUtils.readBlockState(blocks, tag.getCompoundOrEmpty("state"));
				CompoundTag blockEntity = tag.contains("block_entity") ? tag.getCompoundOrEmpty("block_entity") : null;
				out.put(tag.getLongOr("pos", 0L), new Snapshot(state, blockEntity));
			}
		}
	}
}

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
import java.util.stream.Stream;
import net.minecraft.core.BlockPos;
import net.minecraft.core.HolderGetter;
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
import net.minecraft.world.entity.item.FallingBlockEntity;
import net.minecraft.world.entity.item.ItemEntity;
import net.minecraft.world.entity.item.PrimedTnt;
import net.minecraft.world.entity.projectile.arrow.AbstractArrow;
import net.minecraft.world.item.ItemStack;
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
 * <p>Server thread only.
 */
public final class Rollback {
	private static final int MAX_CHECKPOINTS = 256;
	// Back the way it was, with no neighbour updates, drops, block entity side effects (a chest
	// spilling its contents) or placement reactions (TNT lighting).
	private static final int RESTORE_FLAGS = Block.UPDATE_CLIENTS | Block.UPDATE_SKIP_ALL_SIDEEFFECTS;
	private static final EquipmentSlot[] WORN = { EquipmentSlot.HEAD, EquipmentSlot.CHEST, EquipmentSlot.LEGS, EquipmentSlot.FEET, EquipmentSlot.OFFHAND };

	/** A block as it was: its state and its block entity's data (null: none, or not captured). */
	private record Snapshot(BlockState state, @Nullable CompoundTag blockEntity) {
	}

	private static final Long2ObjectOpenHashMap<Snapshot> ORIGINALS = new Long2ObjectOpenHashMap<>();
	private static final Long2ObjectOpenHashMap<Snapshot> LATEST = new Long2ObjectOpenHashMap<>();

	private Rollback() {
	}

	// ---- tracking --------------------------------------------------------------------------

	/** The world opened: picks up what earlier sessions tracked. */
	public static void load(MinecraftServer opened) {
		ORIGINALS.clear();
		LATEST.clear();
		Path file = dir(opened).resolve("blocks.nbt");
		if (!Files.exists(file)) {
			return;
		}
		try {
			CompoundTag root = NbtIo.readCompressed(file, NbtAccounter.unlimitedHeap());
			HolderGetter<Block> blocks = opened.overworld().registryAccess().lookupOrThrow(Registries.BLOCK);
			readSnapshots(root.getListOrEmpty("originals"), blocks, ORIGINALS);
			readSnapshots(root.getListOrEmpty("latest"), blocks, LATEST);
			HalfCraft.LOG.info("HalfCraft: rollback tracks {} blocks", ORIGINALS.size());
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
		ListTag blocks = new ListTag();
		for (long key : ORIGINALS.keySet()) {
			LevelChunk chunk = loadedChunk(level, key);
			Snapshot now = chunk != null ? capture(chunk, BlockPos.of(key)) : LATEST.getOrDefault(key, ORIGINALS.get(key));
			LATEST.put(key, now);
			blocks.add(writeSnapshot(key, now));
		}
		CompoundTag root = new CompoundTag();
		root.put("blocks", blocks);
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
		Long2ObjectOpenHashMap<Snapshot> target = new Long2ObjectOpenHashMap<>();
		readSnapshots(root.getListOrEmpty("blocks"), level.registryAccess().lookupOrThrow(Registries.BLOCK), target);

		LongOpenHashSet keys = new LongOpenHashSet(ORIGINALS.keySet());
		keys.addAll(target.keySet());
		int changed = 0;
		for (long key : keys) {
			Snapshot want = target.containsKey(key) ? target.get(key) : ORIGINALS.get(key);
			if (want == null) {
				continue;
			}
			Snapshot have = LATEST.get(key);
			if (have != null && have.state() == want.state() && want.blockEntity() == null) {
				continue;  // already so (nothing to load, and its chunk can stay where it is)
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
			changed++;
		}

		// What was flying or lying about since is gone (and whatever a removed chest held).
		List<Entity> transient_ = new ArrayList<>();
		for (Entity entity : level.getAllEntities()) {
			if (entity instanceof ItemEntity || entity instanceof AbstractArrow || entity instanceof PrimedTnt || entity instanceof FallingBlockEntity
				|| entity instanceof ExperienceOrb) {
				transient_.add(entity);
			}
		}
		transient_.forEach(Entity::discard);

		if (player != null && root.contains("player")) {
			loadPlayer(player, root.getCompoundOrEmpty("player"));
		}
		HalfCraft.LOG.info("HalfCraft: rolled back to checkpoint {}: {} blocks, {} things removed{}", Long.toHexString(id), changed, transient_.size(),
			player != null ? ", the player" : "");
	}

	// ---- the player ----------------------------------------------------------------------------

	private static CompoundTag savePlayer(ServerPlayer player) {
		TagValueOutput out = TagValueOutput.createWithContext(ProblemReporter.DISCARDING, player.registryAccess());
		player.getInventory().save(out.list("inventory", ItemStackWithSlot.CODEC));
		for (EquipmentSlot slot : WORN) {
			out.store(slot.getName(), ItemStack.OPTIONAL_CODEC, player.getItemBySlot(slot));
		}
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
		player.getInventory().load(in.listOrEmpty("inventory", ItemStackWithSlot.CODEC));
		for (EquipmentSlot slot : WORN) {
			player.setItemSlot(slot, in.read(slot.getName(), ItemStack.OPTIONAL_CODEC).orElse(ItemStack.EMPTY));
		}
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

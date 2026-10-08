package dev.halfcraft.client;

import dev.halfcraft.combat.HostCombat;
import dev.halfcraft.combat.HostHurts;
import dev.halfcraft.link.Proto;
import dev.halfcraft.link.HostLink;
import dev.halfcraft.link.HostPush;
import dev.halfcraft.link.HostStrings;
import dev.halfcraft.world.HostBlockDamage;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.screens.PauseScreen;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.client.input.KeyEvent;
import net.minecraft.client.input.MouseButtonInfo;
import net.minecraft.core.BlockPos;
import net.minecraft.world.phys.Vec3;
import org.lwjgl.sdl.SDLKeyboard;

/**
 * Replays Half-Life-captured input into Minecraft's own input handlers, as if the (hidden) MC
 * window had focus. Keeps a virtual keyboard so InputConstants.isKeyDown() still works.
 */
public final class InputBridge {
	private static final boolean[] KEYS = new boolean[512];
	private static final boolean[] BUTTONS = new boolean[8];
	private static double cursorX, cursorY;
	private static int modifiers;
	private static int clickLogs;
	// A restore that came before Minecraft's world was open (Half-Life loaded a save first thing).
	private static long pendingRestore;
	// Where the next hurt came from (Proto.IN_HURT_FROM comes right before its IN_HURT).
	private static Vec3 hurtFrom;

	private InputBridge() {
	}

	public static boolean isKeyDown(int scancode) {
		return scancode >= 0 && scancode < KEYS.length && KEYS[scancode];
	}

	public static void drain(Minecraft minecraft) {
		HostLink.drainInput((type, code, a, b, c) -> dispatch(minecraft, type, code, a, b, c));
	}

	private static void dispatch(Minecraft minecraft, int type, int code, int a, int b, int c) {
		long handle = minecraft.getWindow().handle();
		switch (type) {
			case Proto.IN_KEY -> key(minecraft, handle, code, a != 0);
			case Proto.IN_MOUSE_BUTTON -> {
				if (code > 0 && code < BUTTONS.length) {
					BUTTONS[code] = a != 0;
				}
				if (a != 0 && clickLogs++ < 20) {
					var hit = minecraft.hitResult;
					dev.halfcraft.HalfCraft.LOG.info("HalfCraft: click {} -> {} {} (grabbed {}, screen {})", code, hit == null ? "null" : hit.getType(),
						hit instanceof net.minecraft.world.phys.EntityHitResult eh ? eh.getEntity().getName().getString() : hit == null ? "" : hit.getLocation(),
						minecraft.mouseHandler.isMouseGrabbed(), minecraft.gui.screen());
				}
				minecraft.mouseHandler.onButton(handle, new MouseButtonInfo(code, modifiers), a != 0 ? 1 : 0);
			}
			case Proto.IN_SCROLL -> minecraft.mouseHandler.onScroll(handle, 0.0, a / 120.0);
			case Proto.IN_CURSOR -> {
				double dx = a - cursorX;
				double dy = b - cursorY;
				cursorX = a;
				cursorY = b;
				minecraft.mouseHandler.onMove(handle, a, b, dx, dy);
			}
			case Proto.IN_TEXT -> {
				if (minecraft.gui.screen() != null) {
					minecraft.keyboardHandler.textInput(handle, new String(Character.toChars(a)));
				}
			}
			case Proto.IN_RELEASE_ALL -> releaseAll();
			case Proto.IN_HURT_FROM -> hurtFrom = new Vec3(HostHurts.coordinate(a), HostHurts.coordinate(b), HostHurts.coordinate(c));
			case Proto.IN_HURT -> hurt(minecraft, code, a / 100.0F, b, c);
			case Proto.IN_HEAL -> heal(minecraft, code, a / 100.0F);
			case Proto.IN_HURT_MOB -> dev.halfcraft.mobs.HostMobs.hurtFromHost(minecraft.getSingleplayerServer(), code, a, b / 100.0F, c);
			case Proto.IN_BLAST -> HostBlockDamage.blast(minecraft.getSingleplayerServer(), HostHurts.coordinate(a), HostHurts.coordinate(b),
				HostHurts.coordinate(c), code / 100.0F);
			case Proto.IN_BULLET_HIT -> HostBlockDamage.bulletHit(minecraft.getSingleplayerServer(), new BlockPos(a, b, c));
			case Proto.IN_CHECKPOINT -> rollback(minecraft, checkpointId(a, b), false);
			case Proto.IN_RESTORE -> rollback(minecraft, checkpointId(a, b), true);
			case Proto.IN_STRING -> HostStrings.accept(code, a, b, c);
			case Proto.IN_PUSH -> HostPush.INSTANCE.accept(a, b, c, System.currentTimeMillis());
			case Proto.IN_IMPULSE -> HostPush.INSTANCE.impulse(a, b, c);
			case Proto.IN_OPEN_MENU -> {
				if (minecraft.gui.screen() == null && minecraft.player != null) {
					releaseAll();
					minecraft.gui.setScreen(new PauseScreen(true));
				}
			}
			default -> {
			}
		}
	}

	private static long checkpointId(int low, int high) {
		return (high & 0xFFFFFFFFL) << 32 | (low & 0xFFFFFFFFL);
	}

	/** Half-Life saved (a checkpoint) or loaded a save (a restore): Rollback, on the integrated server. */
	private static void rollback(Minecraft minecraft, long id, boolean restore) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null) {
			if (restore) {
				pendingRestore = id;
			} else {
				dev.halfcraft.HalfCraft.LOG.info("HalfCraft: Half-Life saved before Minecraft's world was open; no checkpoint {}", Long.toHexString(id));
			}
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (restore) {
				dev.halfcraft.world.Rollback.restore(server, id, player);
			} else {
				dev.halfcraft.world.Rollback.checkpoint(server, id, player);
			}
		});
	}

	/** Every client tick: a restore that was waiting for the world. */
	public static void tick(Minecraft minecraft) {
		if (pendingRestore != 0 && minecraft.player != null && minecraft.getSingleplayerServer() != null) {
			long id = pendingRestore;
			pendingRestore = 0;
			rollback(minecraft, id, true);
		}
	}

	/** Half-Life hit the player: apply it as Minecraft damage on the integrated server (or the host's). */
	private static void hurt(Minecraft minecraft, int kind, float hostDamage, int attacker, int flags) {
		Vec3 from = hurtFrom;
		hurtFrom = null;
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null) {
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player != null) {
				HostCombat.hurtPlayer(player, kind, hostDamage, attacker, flags, from);
			}
		});
	}

	private static void heal(Minecraft minecraft, int kind, float hostPoints) {
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null) {
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player != null) {
				HostCombat.healPlayer(player, kind, hostPoints);
			}
		});
	}

	private static void key(Minecraft minecraft, long handle, int scancode, boolean down) {
		if (scancode <= 0 || scancode >= KEYS.length) {
			return;
		}
		boolean wasDown = KEYS[scancode];
		KEYS[scancode] = down;
		updateModifiers();
		int action = down ? (wasDown ? -1 : 1) : 0; // -1 = repeat
		int keycode = SDLKeyboard.SDL_GetKeyFromScancode(scancode, (short) modifiers, true);
		minecraft.keyboardHandler.keyPress(handle, action, new KeyEvent(scancode, keycode, modifiers));
	}

	private static void updateModifiers() {
		int m = 0;
		if (KEYS[225]) m |= 0x0001; // SDL_KMOD_LSHIFT
		if (KEYS[229]) m |= 0x0002; // SDL_KMOD_RSHIFT
		if (KEYS[224]) m |= 0x0040; // SDL_KMOD_LCTRL
		if (KEYS[228]) m |= 0x0080; // SDL_KMOD_RCTRL
		if (KEYS[226]) m |= 0x0100; // SDL_KMOD_LALT
		if (KEYS[230]) m |= 0x0200; // SDL_KMOD_RALT
		modifiers = m;
	}

	/** Lift every key and button we think is held (focus moved to Half-Life, link dropped, ...). */
	public static void releaseAll() {
		Minecraft minecraft = Minecraft.getInstance();
		long handle = minecraft.getWindow().handle();
		for (int sc = 0; sc < KEYS.length; sc++) {
			if (KEYS[sc]) {
				KEYS[sc] = false;
				updateModifiers();
				minecraft.keyboardHandler.keyPress(handle, 0, new KeyEvent(sc, SDLKeyboard.SDL_GetKeyFromScancode(sc, (short) 0, true), modifiers));
			}
		}
		for (int button = 1; button < BUTTONS.length; button++) {
			if (BUTTONS[button]) {
				BUTTONS[button] = false;
				minecraft.mouseHandler.onButton(handle, new MouseButtonInfo(button, 0), 0);
			}
		}
	}
}

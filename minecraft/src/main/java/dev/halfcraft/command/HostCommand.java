package dev.halfcraft.command;

import dev.halfcraft.HalfCraft;
import net.minecraft.commands.CommandSource;
import net.minecraft.commands.CommandSourceStack;
import net.minecraft.network.chat.Component;
import net.minecraft.server.level.ServerPlayer;
import net.minecraft.server.permissions.LevelBasedPermissionSet;

/**
 * Minecraft commands from Half-Life's console (hc_mc, string channel STR_COMMAND). They run as the
 * player with an owner's permission, like a command typed into chat in a world with cheats on. What
 * a command answers goes to the player's chat and to the log ("HalfCraft: hc_mc ..."), where test
 * scripts wait for it (tools/wait_log.py).
 */
public final class HostCommand {
	private HostCommand() {
	}

	/** {@code raw} as a command to run: without surrounding blanks and a leading '/'; null when that leaves nothing. */
	public static String normalize(String raw) {
		if (raw == null) {
			return null;
		}
		String command = raw.strip();
		if (command.startsWith("/")) {
			command = command.substring(1).strip();
		}
		return command.isEmpty() ? null : command;
	}

	/** Runs {@code command} (normalized) as {@code player}. Server thread only. */
	public static void run(ServerPlayer player, String command) {
		HalfCraft.LOG.info("HalfCraft: hc_mc {}", command);
		CommandSourceStack source = player.createCommandSourceStack()
			.withSource(new Feedback(player, command))
			.withPermission(LevelBasedPermissionSet.OWNER)
			.withCallback((success, result) -> HalfCraft.LOG.info("HalfCraft: hc_mc {} finished: {} ({})", command, success ? "success" : "failure", result));
		player.level().getServer().getCommands().performPrefixedCommand(source, command);
	}

	/** Where a command's answers go: the player's chat, and the log line scripts look for. */
	private record Feedback(ServerPlayer player, String command) implements CommandSource {
		@Override
		public void sendSystemMessage(Component message) {
			HalfCraft.LOG.info("HalfCraft: hc_mc {} -> {}", command, message.getString());
			player.sendSystemMessage(message);
		}

		@Override
		public boolean acceptsSuccess() {
			return true;
		}

		@Override
		public boolean acceptsFailure() {
			return true;
		}

		// the player is the only admin: telling them again in grey would only repeat the answer
		@Override
		public boolean shouldInformAdmins() {
			return false;
		}
	}
}

package dev.halfcraft.client;

import dev.halfcraft.HalfCraft;
import dev.halfcraft.command.HostCommand;
import dev.halfcraft.link.HostStrings;
import dev.halfcraft.link.Proto;
import net.minecraft.client.Minecraft;
import net.minecraft.server.level.ServerPlayer;

/** Half-Life's hc_mc: the commands it sends (STR_COMMAND) run on the integrated server, as the player. */
public final class HostCommands {
	private HostCommands() {
	}

	public static void register() {
		HostStrings.register(Proto.STR_COMMAND, HostCommands::run);
	}

	private static void run(String raw) {
		String command = HostCommand.normalize(raw);
		if (command == null) {
			HalfCraft.LOG.warn("HalfCraft: hc_mc came with no command");
			return;
		}
		Minecraft minecraft = Minecraft.getInstance();
		var server = minecraft.getSingleplayerServer();
		if (minecraft.player == null || server == null) {
			HalfCraft.LOG.warn("HalfCraft: hc_mc {} dropped: no world is open", command);
			return;
		}
		var uuid = minecraft.player.getUUID();
		server.execute(() -> {
			ServerPlayer player = server.getPlayerList().getPlayer(uuid);
			if (player != null) {
				HostCommand.run(player, command);
			}
		});
	}
}

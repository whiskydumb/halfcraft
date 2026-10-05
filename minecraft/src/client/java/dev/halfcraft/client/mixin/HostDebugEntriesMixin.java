package dev.halfcraft.client.mixin;

import dev.halfcraft.client.debug.HostDebugScreen;
import java.util.Map;
import net.minecraft.client.gui.components.debug.DebugScreenEntries;
import net.minecraft.client.gui.components.debug.DebugScreenEntry;
import net.minecraft.resources.Identifier;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Half-Life's debug screen entries ({@link HostDebugScreen}) join Minecraft's registry of them as it
 * is built, so the debug screen and its options list them like its own. Registering any later could
 * miss the debug profile, which Minecraft loads while it starts up.
 */
@Mixin(DebugScreenEntries.class)
public abstract class HostDebugEntriesMixin {
	@Inject(method = "<clinit>", at = @At("TAIL"))
	private static void halfcraft$registerHostEntries(CallbackInfo ci) {
		for (Map.Entry<Identifier, DebugScreenEntry> entry : HostDebugScreen.ENTRIES.entrySet()) {
			DebugScreenEntries.register(entry.getKey(), entry.getValue());
		}
	}
}

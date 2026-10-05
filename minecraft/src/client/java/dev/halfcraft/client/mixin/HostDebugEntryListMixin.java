package dev.halfcraft.client.mixin;

import dev.halfcraft.client.debug.HostDebugScreen;
import java.util.Map;
import net.minecraft.client.gui.components.debug.DebugScreenEntryList;
import net.minecraft.client.gui.components.debug.DebugScreenEntryStatus;
import net.minecraft.resources.Identifier;
import org.spongepowered.asm.mixin.Final;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.Shadow;
import org.spongepowered.asm.mixin.injection.At;
import org.spongepowered.asm.mixin.injection.Inject;
import org.spongepowered.asm.mixin.injection.callback.CallbackInfo;

/**
 * Half-Life's debug screen entries are on in the F3 overlay unless the player turned them off:
 * Minecraft's profiles (and a debug profile saved before HalfCraft) don't know them, and an entry
 * missing from the statuses never shows.
 */
@Mixin(DebugScreenEntryList.class)
public abstract class HostDebugEntryListMixin {
	@Shadow
	@Final
	private Map<Identifier, DebugScreenEntryStatus> allStatuses;

	@Inject(method = "resetStatuses", at = @At("TAIL"))
	private void halfcraft$showHostEntries(Map<Identifier, DebugScreenEntryStatus> statuses, CallbackInfo ci) {
		for (Identifier id : HostDebugScreen.ENTRIES.keySet()) {
			allStatuses.putIfAbsent(id, DebugScreenEntryStatus.IN_OVERLAY);
		}
	}
}

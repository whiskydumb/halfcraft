package dev.halfcraft.client.mixin;

import java.io.File;
import net.minecraft.client.Screenshot;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

@Mixin(Screenshot.class)
public interface ScreenshotAccessor {
	/** Vanilla's screenshot file name in {@code folder}: the date and time, numbered when taken. */
	@Invoker("getFile")
	static File halfcraft$getFile(File folder) {
		throw new AssertionError();
	}
}

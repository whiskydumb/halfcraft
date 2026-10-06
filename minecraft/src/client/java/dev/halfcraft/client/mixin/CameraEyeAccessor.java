package dev.halfcraft.client.mixin;

import net.minecraft.client.Camera;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Accessor;

/** The camera's smoothed eye height, which DuckJump keeps steady while the feet move. */
@Mixin(Camera.class)
public interface CameraEyeAccessor {
	@Accessor("eyeHeight")
	float halfcraft$eyeHeight();

	@Accessor("eyeHeight")
	void halfcraft$setEyeHeight(float height);

	@Accessor("eyeHeightOld")
	float halfcraft$eyeHeightOld();

	@Accessor("eyeHeightOld")
	void halfcraft$setEyeHeightOld(float height);
}

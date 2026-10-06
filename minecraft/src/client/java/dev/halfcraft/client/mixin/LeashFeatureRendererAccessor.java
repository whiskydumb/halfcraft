package dev.halfcraft.client.mixin;

import com.mojang.blaze3d.vertex.VertexConsumer;
import net.minecraft.client.renderer.entity.state.EntityRenderState;
import net.minecraft.client.renderer.feature.LeashFeatureRenderer;
import org.joml.Matrix4fc;
import org.spongepowered.asm.mixin.Mixin;
import org.spongepowered.asm.mixin.gen.Invoker;

/** Minecraft's leash geometry (one step of its strip), for AvatarExporter to capture leashes as Minecraft draws them. */
@Mixin(LeashFeatureRenderer.class)
public interface LeashFeatureRendererAccessor {
	@Invoker("addVertexPair")
	static void halfcraft$addVertexPair(VertexConsumer buffer, Matrix4fc pose, float dx, float dy, float dz, float width, float sideX, float sideZ, int step,
		boolean back, EntityRenderState.LeashState leash) {
		throw new AssertionError();
	}
}

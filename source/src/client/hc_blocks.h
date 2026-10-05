#pragma once

// client.dll: minecraft's blocks drawn in source's world. minecraft meshes every 16x16x16 section
// with its own block renderer (models, tint, ambient occlusion) and ships it with its texture atlas
// over the render ring; each section becomes a source renderable, so the leaf system culls it,
// half-life's walls hide it and its fog covers it. light comes from the map's lightmaps where the
// blocks stand, plus minecraft's own block light (torches, lava, glowstone).

#include <cstdint>

#include "core/hc_units.h"

class IMaterial;
class ITexture;

namespace halfcraft
{
	/// minecraft's block/item atlas as drawn here (null before minecraft sent it). its uvs are
	/// minecraft's times u_scale / v_scale (the texture is padded to a power of two).
	struct AtlasView
	{
		ITexture*  texture;
		IMaterial* cutout;
		IMaterial* translucent;
		float      u_scale;
		float      v_scale;
	};

	AtlasView blocks_atlas();

	/// whether minecraft's block at (x, y, z) is solid (one npcs collide with).
	bool blocks_solid(int x, int y, int z);

	/// one render-ring message (main thread).
	void blocks_on_message(std::uint32_t type, const std::uint8_t* payload, std::uint32_t bytes);

	/// the current map's slot (where minecraft's sections land in source); drops sections that
	/// belong to another map's slot.
	void blocks_set_slot(MapSlot slot);

	/// once per frame, main thread: builds the meshes of sections that changed, within a budget.
	void blocks_update();

	/// drops every section (map change, minecraft gone).
	void blocks_clear();

	/// releases the atlas and materials (client shutdown).
	void blocks_shutdown();

	struct SolidSection;
	/// minecraft's solid blocks for server.dll (see shared/hc_bridge.h, SolidsSinceFn).
	int solids_since(std::uint32_t since, SolidSection* out, int max, std::uint32_t* now);
}

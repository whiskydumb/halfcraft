#pragma once

// client.dll: minecraft's images (the block/item atlas, entity skins) as source textures, and the
// unlit materials that draw them. include after the sdk headers and tier0/valve_minmax_off.h.

#include <cstdint>
#include <vector>

#include "materialsystem/itexture.h"

class IMaterial;

namespace halfcraft
{
	/// a minecraft image as a procedural texture: point sampled, no mips, padded to a power of two
	/// (uvs scale by u_scale / v_scale to match).
	class McTexture final : public ITextureRegenerator
	{
	public:
		/// @param name - prefix of the texture's name (a counter makes it unique)
		explicit McTexture(const char* name) : name_(name) {}
		~McTexture() { release(); }
		McTexture(const McTexture&) = delete;
		McTexture& operator=(const McTexture&) = delete;

		/// the whole image, rgba8 top row first. returns true when its size changed (the texture
		/// object is new then).
		bool set(const std::uint8_t* rgba, int width, int height);
		/// part of the image (an animated sprite's current frame).
		void update_region(int x, int y, int width, int height, const std::uint8_t* rgba);
		void release();

		[[nodiscard]] ITexture* texture() const { return texture_; }
		[[nodiscard]] float     u_scale() const { return texture_wide_ ? float(width_) / float(texture_wide_) : 1.0f; }
		[[nodiscard]] float     v_scale() const { return texture_tall_ ? float(height_) / float(texture_tall_) : 1.0f; }

		// ITextureRegenerator
		void RegenerateTextureBits(ITexture* texture, IVTFTexture* vtf, Rect_t* rect) override;
		void Release() override {}  // owned here, not by the texture

	private:
		void copy(const std::uint8_t* rgba, int x0, int y0, int width, int height);

		const char*               name_;
		std::vector<std::uint8_t> bgra_;
		int                       width_ = 0, height_ = 0;
		int                       texture_wide_ = 0, texture_tall_ = 0;
		ITexture*                 texture_ = nullptr;
	};

	enum class Blend
	{
		CUTOUT,       // alpha tested
		TRANSLUCENT,  // alpha blended
		DECAL,        // alpha blended, pulled in front of the surface it lies on (cracks)
		OUTLINE,      // alpha blended vertex colour only, pulled forward like a decal (outlines)
	};

	/// an UnlitGeneric material lit by its vertex colours, drawn from both sides.
	/// @param name - prefix of the material's name (a counter makes it unique)
	/// @param texture - the base texture (ignored for Blend::OUTLINE)
	IMaterial* make_unlit_material(const char* name, ITexture* texture, Blend blend);
	/// points a material made by make_unlit_material at another texture.
	void set_material_texture(IMaterial* material, ITexture* texture);
	void release_material(IMaterial*& material);
}

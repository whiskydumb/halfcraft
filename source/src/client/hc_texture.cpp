// client.dll: minecraft's images as source textures (see hc_texture.h).

#include "cbase.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialvar.h"
#include "texture_group_names.h"
#include "vtf/vtf.h"
#include "KeyValues.h"

#include "tier0/valve_minmax_off.h"
#include <cstring>

#include "client/hc_texture.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	namespace
	{
		int next_power_of_two(int value)
		{
			int result = 1;
			while (result < value) {
				result <<= 1;
			}
			return result;
		}

		/// procedural textures and materials keep their names until the engine frees them: never reuse one.
		void unique_name(char* out, std::size_t size, const char* prefix)
		{
			static int counter = 0;
			Q_snprintf(out, size, "halfcraft/%s%d", prefix, ++counter);
		}
	}

	bool McTexture::set(const std::uint8_t* rgba, int width, int height)
	{
		const bool resized = width != width_ || height != height_;
		if (resized) {
			release();
			width_ = width;
			height_ = height;
			texture_wide_ = next_power_of_two(width);
			texture_tall_ = next_power_of_two(height);
			bgra_.assign(static_cast<std::size_t>(texture_wide_) * texture_tall_ * 4, 0);
		}
		copy(rgba, 0, 0, width, height);
		if (!texture_) {
			char name[64];
			unique_name(name, sizeof(name), name_);
			texture_ = materials->CreateProceduralTexture(name, TEXTURE_GROUP_OTHER, texture_wide_, texture_tall_, IMAGE_FORMAT_BGRA8888,
				TEXTUREFLAGS_POINTSAMPLE | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_PROCEDURAL | TEXTUREFLAGS_SINGLECOPY |
					TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT);
			texture_->SetTextureRegenerator(this);
		}
		texture_->Download();
		return resized;
	}

	void McTexture::update_region(int x, int y, int width, int height, const std::uint8_t* rgba)
	{
		if (!texture_ || x < 0 || y < 0 || x + width > width_ || y + height > height_) {
			return;
		}
		copy(rgba, x, y, width, height);
		Rect_t rect{ x, y, width, height };
		texture_->Download(&rect);
	}

	void McTexture::release()
	{
		if (texture_) {
			texture_->SetTextureRegenerator(nullptr);
			texture_->DecrementReferenceCount();
			texture_->DeleteIfUnreferenced();
			texture_ = nullptr;
		}
	}

	void McTexture::RegenerateTextureBits(ITexture* /*texture*/, IVTFTexture* vtf, Rect_t* rect)
	{
		if (vtf->Format() != IMAGE_FORMAT_BGRA8888 || vtf->Width() != texture_wide_ || vtf->Height() != texture_tall_) {
			return;
		}
		const int x0 = rect ? rect->x : 0, y0 = rect ? rect->y : 0;
		const int w = rect ? rect->width : texture_wide_, h = rect ? rect->height : texture_tall_;
		std::uint8_t* dst = vtf->ImageData(0, 0, 0);
		const int     row = texture_wide_ * 4;
		for (int y = y0; y < y0 + h; ++y) {
			std::memcpy(dst + y * row + x0 * 4, bgra_.data() + static_cast<std::size_t>(y) * row + x0 * 4, static_cast<std::size_t>(w) * 4);
		}
	}

	void McTexture::copy(const std::uint8_t* rgba, int x0, int y0, int width, int height)
	{
		for (int y = 0; y < height; ++y) {
			const std::uint8_t* src = rgba + static_cast<std::size_t>(y) * width * 4;
			std::uint8_t*       dst = bgra_.data() + (static_cast<std::size_t>(y0 + y) * texture_wide_ + x0) * 4;
			for (int x = 0; x < width; ++x, src += 4, dst += 4) {
				dst[0] = src[2];
				dst[1] = src[1];
				dst[2] = src[0];
				dst[3] = src[3];
			}
		}
	}

	IMaterial* make_unlit_material(const char* name, ITexture* texture, Blend blend)
	{
		auto* values = new KeyValues("UnlitGeneric");
		if (blend != Blend::OUTLINE && texture) {
			values->SetString("$basetexture", texture->GetName());
		}
		values->SetInt("$vertexcolor", 1);
		values->SetInt("$nocull", 1);
		if (blend == Blend::CUTOUT) {
			values->SetInt("$alphatest", 1);
			values->SetFloat("$alphatestreference", 0.5f);
		} else {
			values->SetInt("$translucent", 1);
			values->SetInt("$vertexalpha", 1);
		}
		if (blend == Blend::DECAL || blend == Blend::OUTLINE) {
			values->SetInt("$decal", 1);
		}
		char unique[64];
		unique_name(unique, sizeof(unique), name);
		IMaterial* material = materials->CreateMaterial(unique, values);  // comes with one reference, ours
		// a created material isn't precached (no shader, no vertex format) until a level load caches
		// what's in use, or it's first drawn; static meshes need its vertex format before that
		if (!material->IsPrecached()) {
			materials->CacheUsedMaterials();
		}
		return material;
	}

	void set_material_texture(IMaterial* material, ITexture* texture)
	{
		bool found = false;
		if (IMaterialVar* var = material->FindVar("$basetexture", &found, false); found && texture) {
			var->SetTextureValue(texture);
		}
	}

	void release_material(IMaterial*& material)
	{
		if (material) {
			material->DecrementReferenceCount();
			material->DeleteIfUnreferenced();
			material = nullptr;
		}
	}
}

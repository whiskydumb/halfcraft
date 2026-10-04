#pragma once

// client.dll: triangles of minecraft geometry in source's world space, as static meshes (blocks)
// or drawn straight from memory each frame (entities, particles). include after the sdk headers.

#include <cstdint>

class IMaterial;
class IMesh;

namespace halfcraft
{
	struct DrawVertex
	{
		float        position[3];  // source world space
		std::uint8_t color[4];
		float        uv[2];
	};

	/// a static mesh of `count` vertices (a triangle list) in the material's vertex format.
	/// @return nullptr when there's nothing to make
	IMesh* make_static_mesh(IMaterial* material, const DrawVertex* vertices, int count);
	void   destroy_static_mesh(IMesh* mesh);

	/// draws a triangle list now through the dynamic mesh, in as many pieces as it takes.
	void draw_triangles(IMaterial* material, const DrawVertex* vertices, int count);
}

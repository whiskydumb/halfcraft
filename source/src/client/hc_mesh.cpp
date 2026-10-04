// client.dll: minecraft geometry as source meshes (see hc_mesh.h).

#include "cbase.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "texture_group_names.h"

#include "tier0/valve_minmax_off.h"
#include <algorithm>

#include "client/hc_mesh.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace halfcraft
{
	IMesh* make_static_mesh(IMaterial* material, const DrawVertex* vertices, int count)
	{
		count = count / 3 * 3;
		if (count < 3 || !material || !material->IsPrecached()) {
			return nullptr;  // an uncached material has no vertex format yet (the shader api divides by its size)
		}
		// the mesh must carry exactly what the material's shader reads (a normal too, maybe compressed)
		const VertexFormat_t format = material->GetVertexFormat();
		const bool           has_normal = (format & VERTEX_NORMAL) != 0;
		const bool           compressed = CompressionType(format) == VERTEX_COMPRESSION_ON;
		CMatRenderContextPtr context(materials);
		IMesh* mesh = context->CreateStaticMesh(format, TEXTURE_GROUP_STATIC_VERTEX_BUFFER_WORLD, material);
		if (!mesh) {
			return nullptr;
		}
		CMeshBuilder builder;
		builder.Begin(mesh, MATERIAL_TRIANGLES, count / 3);
		for (int i = 0; i < count; ++i) {
			const auto& v = vertices[i];
			builder.Position3f(v.position[0], v.position[1], v.position[2]);
			if (has_normal) {
				if (compressed) {
					builder.CompressedNormal3f<VERTEX_COMPRESSION_ON>(0.0f, 0.0f, 1.0f);
				} else {
					builder.Normal3f(0.0f, 0.0f, 1.0f);
				}
			}
			builder.Color4ub(v.color[0], v.color[1], v.color[2], v.color[3]);
			builder.TexCoord2f(0, v.uv[0], v.uv[1]);
			builder.AdvanceVertex();
		}
		builder.End();
		return mesh;
	}

	void destroy_static_mesh(IMesh* mesh)
	{
		if (mesh) {
			CMatRenderContextPtr context(materials);
			context->DestroyStaticMesh(mesh);
		}
	}

	void draw_triangles(IMaterial* material, const DrawVertex* vertices, int count)
	{
		count = count / 3 * 3;
		if (count < 3 || !material) {
			return;
		}
		CMatRenderContextPtr context(materials);
		context->Bind(material);
		while (count >= 3) {
			IMesh* mesh = context->GetDynamicMesh(true, nullptr, nullptr, material);
			int    max_vertices = 0, max_indices = 0;
			context->GetMaxToRender(mesh, false, &max_vertices, &max_indices);
			const int batch = std::min({ count, max_vertices, max_indices }) / 3 * 3;
			if (batch < 3) {
				return;
			}
			CMeshBuilder builder;
			builder.Begin(mesh, MATERIAL_TRIANGLES, batch / 3);
			for (int i = 0; i < batch; ++i) {
				const auto& v = vertices[i];
				builder.Position3f(v.position[0], v.position[1], v.position[2]);
				builder.Color4ub(v.color[0], v.color[1], v.color[2], v.color[3]);
				builder.TexCoord2f(0, v.uv[0], v.uv[1]);
				builder.AdvanceVertex();
			}
			builder.End();
			mesh->Draw();
			vertices += batch;
			count -= batch;
		}
	}
}

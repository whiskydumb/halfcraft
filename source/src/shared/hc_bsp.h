#pragma once

// client.dll and server.dll: a map's .bsp read straight from the game's files, lump by lump (the grid's
// floors, hc_floors.cpp; the sky's brushes, hc_world_collision.cpp). include after cbase.h, bspfile.h,
// filesystem.h and tier0/valve_minmax_off.h.

#include <vector>

namespace halfcraft
{
	namespace bsp
	{
		// the file's header: dheader_t, whose lumps' last field half-life 2's tree still calls fourCC
		struct Lump
		{
			int offset, length, version, uncompressed_size;  // compressed (consoles' maps) unless 0
		};
		struct Header
		{
			int  ident, version;
			Lump lumps[HEADER_LUMPS];
			int  revision;
		};

		// the file's layout, as the engine reads it
		static_assert(sizeof(Header) == sizeof(dheader_t));
		static_assert(sizeof(dplane_t) == 20 && sizeof(dvertex_t) == 12 && sizeof(dedge_t) == 4 && sizeof(texinfo_t) == 72);
		static_assert(sizeof(dface_t) == 56 && sizeof(dmodel_t) == 48 && sizeof(ddispinfo_t) == 176 && sizeof(CDispVert) == 20);
		static_assert(sizeof(dbrush_t) == 12 && sizeof(dbrushside_t) == 8);

		/// a .bsp's lumps, read as they're asked for.
		class BspFile
		{
		public:
			/// @param path - "maps/d1_canals_01.bsp", on the GAME search path
			explicit BspFile(const char* path) : file_(filesystem->Open(path, "rb", "GAME")) {}
			~BspFile()
			{
				if (file_) {
					filesystem->Close(file_);
				}
			}
			BspFile(const BspFile&) = delete;
			BspFile& operator=(const BspFile&) = delete;

			bool open()
			{
				return file_ && filesystem->Read(&header_, sizeof(header_), file_) == sizeof(header_) && header_.ident == IDBSPHEADER &&
					   header_.version >= MINBSPVERSION && header_.version <= BSPVERSION;
			}

			/// a lump as an array of T. false when it's compressed (consoles' maps) or cut short.
			template <typename T>
			bool lump(int index, std::vector<T>& out)
			{
				const Lump& l = header_.lumps[index];
				out.clear();
				if (l.uncompressed_size != 0 || l.length < 0 || l.length % sizeof(T) != 0) {
					return false;
				}
				out.resize(l.length / sizeof(T));
				if (out.empty()) {
					return true;
				}
				filesystem->Seek(file_, l.offset, FILESYSTEM_SEEK_HEAD);
				return filesystem->Read(out.data(), l.length, file_) == l.length;
			}

		private:
			FileHandle_t file_;
			Header       header_{};
		};
	}
}

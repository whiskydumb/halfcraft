#include "hc_module.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace halfcraft
{
	void* find_export(const char* module, const char* name)
	{
		HMODULE handle = ::GetModuleHandleA(module);
		return handle ? reinterpret_cast<void*>(::GetProcAddress(handle, name)) : nullptr;
	}
}

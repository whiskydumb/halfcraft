#pragma once

namespace halfcraft
{
	/// a function another loaded dll of this process exports, by name.
	/// @param module - e.g. "client.dll"
	/// @return the function, or nullptr when the dll or the export isn't there
	void* find_export(const char* module, const char* name);
}

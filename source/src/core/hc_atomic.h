#pragma once

#include <atomic>
#include <type_traits>

namespace halfcraft
{
	/// views a plain shared-memory field as an atomic (c++17 has no std::atomic_ref). the java side
	/// reads and writes the same fields with acquire/release VarHandles.
	/// @param value - a naturally aligned field inside the mapping
	/// @return the field as std::atomic, same address
	template <class T>
	std::atomic<T>& as_atomic(T& value)
	{
		static_assert(sizeof(std::atomic<T>) == sizeof(T), "std::atomic<T> must have T's layout");
		static_assert(alignof(std::atomic<T>) == alignof(T), "std::atomic<T> must have T's alignment");
		static_assert(std::atomic<T>::is_always_lock_free, "shared memory needs lock-free atomics");
		return *reinterpret_cast<std::atomic<T>*>(&value);
	}

	template <class T>
	const std::atomic<T>& as_atomic(const T& value)
	{
		return as_atomic(const_cast<T&>(value));
	}
}

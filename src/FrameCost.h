// FrameCost - what each pass of a plugin costs on the thread it runs on, in microseconds
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see the LICENSE file and the notice at the top of main.cpp.
//
// A pass wraps itself in one FrameCost::Scope with a fixed slot and name; devbench reads Slots(). The average follows about the
// last 20 runs, the worst is kept since the last Reset (a load). The same meter as LightKit.h's Cost (Illuminated, RELight).

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace FrameCost
{
	inline constexpr std::size_t kSlots = 24;

	struct Slot
	{
		std::atomic<const char*>   name{ nullptr };
		std::atomic<float>         usAvg{ 0.0f };
		std::atomic<float>         usMax{ 0.0f };
		std::atomic<std::uint32_t> calls{ 0 };  // runs since the last Reset: the idle proof wants 0 new ones with nothing lit
	};

	[[nodiscard]] inline std::array<Slot, kSlots>& Slots()
	{
		static std::array<Slot, kSlots> slots{};
		return slots;
	}

	inline void Note(std::size_t a_slot, const char* a_name, float a_us)
	{
		if (a_slot >= kSlots) {
			return;
		}
		auto&       s = Slots()[a_slot];
		const float avg = s.usAvg.load(std::memory_order_relaxed);
		s.calls.fetch_add(1, std::memory_order_relaxed);
		s.name.store(a_name, std::memory_order_relaxed);
		s.usAvg.store(avg <= 0.0f ? a_us : avg + (a_us - avg) * 0.05f, std::memory_order_relaxed);
		if (a_us > s.usMax.load(std::memory_order_relaxed)) {
			s.usMax.store(a_us, std::memory_order_relaxed);
		}
	}

	inline void Reset()
	{
		for (auto& s : Slots()) {
			s.usAvg.store(0.0f, std::memory_order_relaxed);
			s.usMax.store(0.0f, std::memory_order_relaxed);
			s.calls.store(0, std::memory_order_relaxed);
		}
	}

	// times its own lifetime into one slot
	class Scope
	{
	public:
		Scope(std::size_t a_slot, const char* a_name) noexcept : slot(a_slot), name(a_name), start(std::chrono::steady_clock::now()) {}
		~Scope() { Note(slot, name, std::chrono::duration<float, std::micro>(std::chrono::steady_clock::now() - start).count()); }
		Scope(const Scope&) = delete;
		Scope& operator=(const Scope&) = delete;

	private:
		std::size_t                           slot{ 0 };
		const char*                           name{ nullptr };
		std::chrono::steady_clock::time_point start{};
	};
}

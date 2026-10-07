#pragma once

// what client.dll and server.dll hand each other directly. both live in the same process: client.dll
// exports these functions and server.dll looks them up by name.
//   solids - minecraft's solid blocks, read off the render ring by the client; the server turns them
//            into collision for npcs, physics and bullets
//   hurts  - half-life's hits on the player, caught by the server; the client puts them on the input
//            ring it owns
//   heals  - half-life's health kits, chargers and suit batteries on the player, seen by the server;
//            likewise
//   inputs - anything else the server has for minecraft's input ring (save checkpoints)
//   holding - whether the player carries a prop with use (server knows); the client sends the mouse
//            buttons to source then, to throw or drop it
//   hazards - minecraft's fire, lava and magma blocks (they come with its block lights); the server
//            burns npcs that stand in them
//   arrows - minecraft's arrows that stuck in an npc (an event the server reads); the client draws
//            them on the npc's bones
//   landings - one of minecraft's jumps source took next to where minecraft put its player, or
//            refused (the player movement, hc_movement.cpp); the client sends minecraft's player there

#include <cstdint>

namespace halfcraft
{
	/// one 16x16x16 section's solid blocks (plain layout: it crosses a dll boundary).
	struct SolidSection
	{
		std::int32_t  sx, sy, sz;  // section coords (minecraft)
		std::uint32_t count;       // solid blocks; 0: none (the section's collision is gone)
		std::uint8_t  bits[512];   // bit (x + 16 z + 256 y), minecraft axes
	};

	/// sections whose solids changed after version since.
	/// @param out - up to max of them
	/// @param now - the current version (pass it back as since next time)
	/// @return how many changed (may exceed max: call again with room for all)
	using SolidsSinceFn = int (*)(std::uint32_t since, SolidSection* out, int max, std::uint32_t* now);

	inline constexpr char HC_SOLIDS_EXPORT[] = "HalfCraft_SolidsSince";

	/// server.dll -> client.dll: half-life hit the player; goes to minecraft as proto::kInHurt
	/// (client.dll owns the input ring).
	/// @param kind - proto::HurtKind
	/// @param damage - half-life's damage (minecraft divides it by 5: 100 hp -> 20)
	/// @param attacker - the attacker's actor id (proto::ActorRecord::id), 0 for none
	using PushHurtFn = void (*)(int kind, float damage, std::uint32_t attacker, std::uint32_t flags);

	inline constexpr char HC_PUSH_HURT_EXPORT[] = "HalfCraft_PushHurt";

	/// server.dll -> client.dll: half-life healed the player; goes to minecraft as proto::kInHeal.
	/// @param kind - proto::HealKind
	/// @param amount - half-life points (minecraft divides them by 5)
	using PushHealFn = void (*)(int kind, float amount);

	inline constexpr char HC_PUSH_HEAL_EXPORT[] = "HalfCraft_PushHeal";

	/// server.dll -> client.dll: an event for minecraft's input ring (proto::InputEvent).
	using PushInputFn = void (*)(int type, int code, int a, int b, int c);

	inline constexpr char HC_PUSH_INPUT_EXPORT[] = "HalfCraft_PushInput";

	/// server.dll -> client.dll, every frame: the player carries a prop (half-life's use pickup).
	using SetHoldingFn = void (*)(int holding);

	inline constexpr char HC_SET_HOLDING_EXPORT[] = "HalfCraft_SetHolding";

	/// what hurts at a minecraft block.
	/// @return proto::BlockHazard (kHazardNone for nothing)
	using HazardAtFn = int (*)(int x, int y, int z);

	inline constexpr char HC_HAZARD_AT_EXPORT[] = "HalfCraft_HazardAt";

	/// server.dll -> client.dll: a minecraft arrow stuck in an npc (proto::kEvArrowStuck).
	/// @param entindex - the npc's entity index (the same on both sides)
	/// @param hit - where it hit, source units
	/// @param direction - its flight direction, source axes, unit length
	using StickArrowFn = void (*)(int entindex, const float hit[3], const float direction[3]);

	inline constexpr char HC_STICK_ARROW_EXPORT[] = "HalfCraft_StickArrow";

	/// server.dll -> client.dll: minecraft's screenshot key (proto::kEvScreenshot, an event the server
	/// reads). the client reads the next finished frame back and answers on proto::kStrScreenshot.
	/// @param request - minecraft's request number (proto::McEvent::actorId)
	using RequestScreenshotFn = void (*)(std::uint32_t request);

	inline constexpr char HC_REQUEST_SCREENSHOT_EXPORT[] = "HalfCraft_RequestScreenshot";

	/// server.dll -> client.dll: source took one of minecraft's jumps next to where minecraft put its
	/// player, which didn't fit there (a pearl against a ledge or a ceiling), or refused it. single
	/// player has no client prediction to see it: the client resyncs minecraft's player once source's
	/// is there.
	/// @param feet - where source put the player, or kept it, source units
	/// @param refused - nonzero: source refused the jump
	using JumpLandedFn = void (*)(const float feet[3], int refused);

	inline constexpr char HC_JUMP_LANDED_EXPORT[] = "HalfCraft_JumpLanded";
}

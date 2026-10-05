// HalfCraft shared-memory protocol: the host game (Half-Life 2's client.dll and server.dll) <->
// the Minecraft Fabric mod. Derived from SkyCraft's protocol (see the README).
//
// This header is the single source of truth for the byte layout. The Java side mirrors it in
// minecraft/src/main/java/dev/halfcraft/link/Proto.java; if you change anything here, change it
// there too and bump kVersion.
//
// All multi-byte values are little-endian. The host creates the mapping; Minecraft opens it.
// Coordinates in this protocol are always Minecraft space (blocks, Y up, Z south) unless noted.
#pragma once

#include <cstdint>

namespace halfcraft::proto
{
	inline constexpr std::uint32_t kMagic = 0x464C4148;  // "HALF"
	inline constexpr std::uint32_t kVersion = 3;
	inline constexpr wchar_t       kMappingName[] = L"Local\\HalfCraft_v1";

	// ---- region offsets ---------------------------------------------------------------------
	inline constexpr std::uint64_t kOffHeader = 0x0;
	inline constexpr std::uint64_t kOffHostState = 0x100;
	inline constexpr std::uint64_t kOffMcState = 0x200;
	inline constexpr std::uint64_t kOffOverlayCtl = 0x300;
	inline constexpr std::uint64_t kOffOverlaySlotHdr = 0x340;  // 3 x 0x40
	inline constexpr std::uint64_t kOffWaterGrid = 0x400;       // host -> MC, see WaterGrid
	inline constexpr std::uint64_t kOffInputRing = 0x1000;
	inline constexpr std::uint64_t kOffCollisionRing = 0x40000;
	inline constexpr std::uint64_t kCollisionRingBytes = 32ull << 20;
	inline constexpr std::uint64_t kOffOverlayPixels = kOffCollisionRing + kCollisionRingBytes;
	inline constexpr std::uint32_t kMaxOverlayW = 3840;
	inline constexpr std::uint32_t kMaxOverlayH = 2160;
	inline constexpr std::uint64_t kOverlaySlotBytes = std::uint64_t(kMaxOverlayW) * kMaxOverlayH * 4;
	inline constexpr std::uint32_t kOverlaySlots = 3;
	inline constexpr std::uint64_t kOffActorTable = 0x12000;     // host -> MC, see ActorTable
	inline constexpr std::uint64_t kOffEventRing = 0x17000;      // MC -> host, see McEvent
	inline constexpr std::uint64_t kOffWorldEntities = 0x1E000;  // MC -> host, see WorldEntities
	inline constexpr std::uint64_t kOffHostDebug = 0x22000;      // host -> MC, see HostDebug
	inline constexpr std::uint64_t kHostDebugBytes = 0x1000;
	inline constexpr std::uint64_t kOffWeaponTable = 0x23000;    // host -> MC, see WeaponTable
	inline constexpr std::uint64_t kWeaponTableBytes = 0x1000;
	inline constexpr std::uint64_t kOffMobTable = 0x24000;       // MC -> host, see MobTable
	inline constexpr std::uint64_t kMobTableBytes = 0x5000;
	inline constexpr std::uint64_t kOffRenderRing = kOffOverlayPixels + kOverlaySlotBytes * kOverlaySlots;
	inline constexpr std::uint64_t kRenderRingBytes = 64ull << 20;
	inline constexpr std::uint64_t kMappingBytes = kOffRenderRing + kRenderRingBytes;

	// ---- header @0x0 ------------------------------------------------------------------------
	struct Header
	{
		std::uint32_t magic;
		std::uint32_t version;
		std::uint32_t hostPid;
		std::uint32_t mcPid;
		std::uint64_t hostHeartbeatMs;  // GetTickCount64() at the host's last frame
		std::uint64_t mcHeartbeatMs;    // GetTickCount64() at MC's last frame
		// a new value each time a host sets the link up; Minecraft starts its side over when it changes.
		// the pid alone can't tell: a restarted game may get the pid of the one before it
		std::uint64_t hostSession;
	};
	static_assert(sizeof(Header) == 0x28);

	// ---- host -> MC state @0x100 (seqlock: seq odd while writing) ---------------------------
	enum HostFlags : std::uint32_t
	{
		kHostInGame = 1u << 0,    // a map is loaded and the player exists
		kHostMenuOpen = 1u << 1,  // a host menu (or console) owns input; MC should drop held keys
		kHostLoading = 1u << 2,   // loading screen / level transition in progress
		// source moves the player itself (a ladder, a lift or train, a vehicle, a scripted scene): Minecraft's
		// player stays at posX/Y/Z every tick, without falling or fall damage, until the hand-back's teleport
		kHostTakeover = 1u << 7,
		// bit 8: kept for the puppet group (A), 9-10 for the world group (C)
	};

	// The host's water around the player, for Minecraft to treat as its own water: swimming,
	// floating, drowning. Seqlock like HostState.
	inline constexpr std::uint32_t kWaterGridSize = 16;
	inline constexpr float         kNoWater = -1.0e30f;

	struct WaterGrid
	{
		std::uint32_t seq;
		std::int32_t  originX, originZ;  // Minecraft block column of surface[0]
		std::uint32_t worldId;           // as in HostState
		float         surface[kWaterGridSize * kWaterGridSize];  // [z * size + x]: MC y of the water surface; kNoWater: none
	};
	static_assert(sizeof(WaterGrid) <= 0xC00);

	struct HostState
	{
		std::uint32_t seq;
		std::uint32_t flags;             // HostFlags
		std::uint32_t worldId;           // the host's map (a hash of its name); never 0 in game
		std::uint32_t collisionEpoch;    // bumps on every level load; MC drops all collision data
		double        posX, posY, posZ;  // the host player's feet, MC coords
		float         yaw, pitch;        // authoritative look (MC degrees)
		std::uint32_t teleportSeq;       // MC teleports its player to pos when this changes
		std::uint32_t viewportW, viewportH;
		float         gameHour;
	};
	static_assert(sizeof(HostState) == 0x40);

	// ---- MC -> host state @0x200 (seqlock) --------------------------------------------------
	enum McFlags : std::uint32_t
	{
		kMcInWorld = 1u << 0,
		kMcScreenOpen = 1u << 1,  // an MC GUI screen (inventory, chat, ...) is open
		kMcOnGround = 1u << 2,
		kMcSneaking = 1u << 3,
		kMcSprinting = 1u << 4,
		kMcDead = 1u << 5,
		kMcSwimming = 1u << 6,
		kMcFlying = 1u << 7,
		// bits 12-13: kept for the puppet group (A), 14-15 for the world group (C)
	};

	struct McState
	{
		std::uint32_t seq;
		std::uint32_t flags;          // McFlags
		double        x, y, z;        // interpolated feet position (MC coords)
		float         yaw, pitch;     // MC rotation (degrees)
		float         eyeHeight;      // blocks above feet
		float         sensitivity;    // MC mouse sensitivity option (0..1)
		std::uint32_t teleportAck;    // last HostState::teleportSeq applied
		std::uint32_t guiScale;
		std::uint64_t frameCounter;
		float         fovDeg;         // effective vertical FOV (includes sprint / fluid modifiers)
		float         bobPhase;       // MC walk-bob phase (interpolated walk distance); 0 if bobbing is off
		float         bobAmount;      // MC walk-bob amplitude
		std::uint32_t pad4C;
		double        eyeX, eyeY, eyeZ;  // MC camera position (interpolated, includes sneak eye lerp)

		// Raw 20 Hz physics ticks, so the host can interpolate on its own frame clock exactly like
		// Minecraft's renderer does with partial ticks (no judder from the two games' frame phase).
		std::int64_t tickQpc;             // QueryPerformanceCounter at the (remainder-corrected) tick
		double       prevX, prevY, prevZ;  // feet at the previous tick
		double       curX, curY, curZ;     // feet at the latest tick
		float        tickEyeO, tickEye;      // Camera's smoothed eye height, previous/latest tick
		float        walkDistO, walkDist;    // walk-bob phase inputs
		float        bobO, bob;              // walk-bob amplitude inputs
		float        tickMs;                 // milliseconds per tick (50 unless /tick rate changed)
		std::uint32_t tickPad;

		// Minecraft's camera (F5): 0 first person, 1 third person behind, 2 third person in front
		// (looking back at the player). cameraDistance is how far Minecraft's camera sits from
		// the eye, after its own zoom collision (Minecraft blocks and the host's triangles).
		std::uint32_t cameraMode;
		float         cameraDistance;

		// The player's health and absorption (golden hearts), in Minecraft points (20 = 10 hearts).
		float         health;
		float         maxHealth;
		float         absorption;
		std::uint32_t healthPad;

		// (#12) the host's weapon the player holds as a Minecraft item (see WeaponTable); 0: none
		std::uint32_t heldWeapon;
		std::uint32_t heldWeaponPad;

		// Bumps each time Minecraft moved its player by itself (an ender pearl, chorus fruit, /tp; not the
		// host's own teleports), in the same tick as curX/Y/Z: the host takes the jump that follows wherever
		// the player fits, and resyncs Minecraft where it doesn't.
		std::uint32_t teleportCount;
		std::uint32_t teleportPad[3];

		// 0xF0-0xFF: kept for the world group (C)
	};
	static_assert(sizeof(McState) == 0xF0);
	static_assert(sizeof(McState) <= 0x100);

	// ---- overlay triple buffer @0x300 --------------------------------------------------------
	// state: bits 0-1 = index of the "middle" slot, bit 2 = middle holds an unread frame.
	// Writer (MC) renders into its private back slot, then xchg(state, back | kDirty) and keeps
	// the returned index as its new back slot. Reader (the host) does xchg(state, front) only when
	// the dirty bit is set and keeps the returned index as its new front slot.
	inline constexpr std::uint32_t kOverlayDirty = 1u << 2;

	struct OverlayCtl
	{
		std::uint32_t state;
		std::uint32_t pad;
		std::uint64_t framesPublished;
	};

	struct OverlaySlotHdr
	{
		std::uint32_t width;
		std::uint32_t height;
		std::uint32_t flags;  // bit0: rows are bottom-up
		std::uint32_t pad;
		std::uint64_t frameId;
		std::uint8_t  reserved[0x40 - 0x18];
	};
	static_assert(sizeof(OverlaySlotHdr) == 0x40);

	// ---- input ring @0x1000 (the host produces, MC consumes) --------------------------------
	inline constexpr std::uint32_t kInputRingEntries = 4096;  // power of two
	inline constexpr std::uint64_t kInputRingHeadOff = 0x00;  // u64, written by the host
	inline constexpr std::uint64_t kInputRingTailOff = 0x40;  // u64, written by MC
	inline constexpr std::uint64_t kInputRingDataOff = 0x80;

	enum InputType : std::uint16_t
	{
		kInKey = 1,          // code = SDL scancode, a = 1 press / 0 release
		kInMouseButton = 2,  // code = SDL button (1 L, 2 M, 3 R, 4 X1, 5 X2), a = 1 press / 0 release
		kInScroll = 3,       // a = wheel notches * 120 (positive = up)
		kInCursor = 4,       // a, b = absolute cursor position in overlay pixels
		kInText = 5,         // a = unicode code point
		kInReleaseAll = 6,   // release every held key/button (input focus left MC)
		kInHurt = 7,         // the host hit the player: code = HurtKind, a = host damage * 100, b = attacker actor id, c = HurtFlags
		kInOpenMenu = 8,     // open Minecraft's pause/options menu
		kInHeal = 9,         // the host healed the player: code = HealKind, a = host points * 100 (Minecraft divides by 5, like damage)
		kInCheckpoint = 10,  // the host saved its game: a/b = the save's checkpoint id (low/high 32 bits); Minecraft
		                     // keeps its world's changed blocks and its player as they are now under that id
		kInRestore = 11,     // the host loaded a save: a/b = its checkpoint id; Minecraft goes back to that checkpoint
		kInString = 12,      // a piece of a UTF-8 string for one of Minecraft's StringChannels: code = channel
		                     // | (bytes in this piece << 8) | kStringEnd on the last piece, a/b/c = up to 12 bytes
		                     // (little-endian, a's low byte first)
		kInHurtMob = 13,     // (#13) the host hurt a Minecraft mob: code = HurtKind, a = its Minecraft entity id,
		                     // b = host damage * 100, c = the attacker's actor id (0: none, kMobAttackerPlayer: the host's player)
		kInPush = 22,        // the host pushes its player (a trigger_push, a conveyor, a point_push: Source's base velocity)
		                     // while Minecraft drives it: a/b/c = Minecraft blocks per second * 1000 along x/y/z, repeated
		                     // every kPushRepeatMs while it lasts. (0, 0, 0): it stopped, and the player keeps the last
		                     // push as momentum (as Source does); nothing for kPushStaleMs: it's over, without momentum
		// 23: kept for the puppet group (A), 26-27 for the world group (C)
		kInHurtFrom = 24,    // where the next kInHurt came from, for Minecraft's shield: a/b/c = Minecraft x/y/z (float bits)
		                     // of the blast's centre or of what dealt it; only that one kInHurt uses it
	};
	inline constexpr std::uint32_t kPushRepeatMs = 200;
	inline constexpr std::uint32_t kPushStaleMs = 500;

	// What a kInString is for; Minecraft collects the pieces of each channel until kStringEnd.
	enum StringChannel : std::uint16_t
	{
		kStrCommand = 1,     // (#11) run as the player, like a command typed into chat (without the '/')
		kStrScreenshot = 2,  // (#6) the answer to Minecraft's kEvScreenshot, or the host's own (request 0), as text:
		                     //   "ok <request> <width> <height> <path>": the frame in a temporary file named
		                     //   halfcraft-screenshot-*.rgb, width * height RGB8 pixels, top row first, nothing else;
		                     //   Minecraft saves it as its own screenshot and deletes the file
		                     //   "fail <request> <reason>": the host had no frame to give (loading, ...)
		// 6: kept for the puppet group (A), 8 for the world group (C)
	};
	inline constexpr std::uint16_t kStringChannelMask = 0xFF;
	inline constexpr std::uint16_t kStringBytesShift = 8;  // 4 bits: 0-12 bytes
	inline constexpr std::uint16_t kStringEnd = 1u << 15;
	inline constexpr std::uint32_t kStringPieceBytes = 12;

	enum HealKind : std::uint16_t
	{
		kHealHealth = 0,
		kHealArmor = 1,  // the host's armour (Half-Life's suit): Minecraft absorption
	};

	enum HurtKind : std::uint16_t
	{
		kHurtMelee = 0,
		kHurtProjectile = 1,
		kHurtMagic = 2,
		kHurtOther = 3,
		kHurtBlast = 4,  // grenades, rockets, barrels: Minecraft's explosion damage (blast protection; a shield facing kInHurtFrom)
		kHurtFire = 5,   // burning: Minecraft's fire damage (fire protection)
		kHurtCrush = 6,  // physics props, vehicles, crushers: damage armour takes
	};

	enum HurtFlags : std::uint32_t
	{
		kHurtPowerAttack = 1u << 1,  // shoves harder (Minecraft knockback)
	};

	// ---- actor table @0x12000 (host -> MC, seqlock) -------------------------------------------
	// Nearby host characters (and breakable things), mirrored in Minecraft as invisible hittable
	// stand-in entities.
	inline constexpr std::uint32_t kMaxActors = 256;

	enum ActorFlags : std::uint32_t
	{
		kActorHostile = 1u << 0,    // hostile to the player right now
		kActorDead = 1u << 1,
		kActorEssential = 1u << 2,
		kActorInCombat = 1u << 3,
	};

	struct ActorRecord
	{
		std::uint32_t id;          // the host's handle for it, stable while it exists
		std::uint32_t flags;       // ActorFlags
		float         x, y, z;     // feet, MC coords
		float         yaw;         // MC degrees
		float         width;       // blocks
		float         height;      // blocks
		float         healthFrac;  // 0..1
		std::uint16_t level;
		std::uint16_t pad;
		char          name[24];    // display name, UTF-8, NUL-terminated (truncated)
	};
	static_assert(sizeof(ActorRecord) == 64);

	struct ActorTable
	{
		std::uint32_t seq;
		std::uint32_t count;
		std::uint8_t  pad[0x40 - 8];
		ActorRecord   actors[kMaxActors];
	};
	static_assert(sizeof(ActorTable) == 0x40 + 64 * kMaxActors);

	// ---- event ring @0x17000 (MC -> host) ------------------------------------------------------
	inline constexpr std::uint32_t kEventRingEntries = 512;  // power of two
	inline constexpr std::uint64_t kEventRingHeadOff = 0x00;  // u64, written by MC
	inline constexpr std::uint64_t kEventRingTailOff = 0x40;  // u64, written by the host
	inline constexpr std::uint64_t kEventRingDataOff = 0x80;

	enum McEventType : std::uint32_t
	{
		kEvHitActor = 1,    // actorId, a = MC damage (after MC's own modifiers), b/c = knockback dir x/z (MC), d = knockback strength
		kEvPlayerDied = 2,  // the Minecraft player died: kill the host's player
		kEvExplosion = 3,   // a Minecraft explosion (TNT, creeper, ...): a/b/c = centre (MC coords), d = radius (blocks),
		                    // attackerId = the Minecraft entity id of the mob that set it off (a creeper); 0: nobody (TNT)
		kEvArrowStuck = 4,  // an arrow stuck in a host actor: actorId, a/b/c = where it hit (MC coords), d = flight yaw,
		                    // flags = flight pitch (float bits), weapon = arrow texture (0 plain, 1 tipped, 2 spectral)
		kEvScreenshot = 5,  // (#6) Minecraft's screenshot key: actorId = Minecraft's request number (1+); the host saves its
		                    // own finished frame (its world, its hud and Minecraft's overlay) and answers on kStrScreenshot
		// 6-7: kept for #12 (the host's weapons), 8-9 for #13 (Minecraft's mobs), 12-13 for the puppet group (A),
		// 16-17 for the world group (C)
	};

	enum HitFlags : std::uint32_t
	{
		kHitCritical = 1u << 0,
		kHitProjectile = 1u << 1,
		kHitSweep = 1u << 2,
		kHitFire = 1u << 3,
	};

	// What landed a kEvHitActor (the host picks the damage type and impact effects by it).
	enum HitWeapon : std::uint32_t
	{
		kWeaponUnarmed = 0,
		kWeaponBlade = 1,   // swords
		kWeaponAxe = 2,
		kWeaponBlunt = 3,   // maces, pickaxes, shovels, hoes, anything else held
		kWeaponPierce = 4,  // tridents, spears
		kWeaponArrow = 5,   // arrows and other projectiles
	};

	struct McEvent
	{
		std::uint32_t type;
		std::uint32_t actorId;
		float         a, b, c, d;
		std::uint32_t flags;
		std::uint32_t weapon;      // HitWeapon for kEvHitActor
		std::uint32_t attackerId;  // (#13) kEvHitActor: the Minecraft entity id of the mob that landed it; 0: the player.
		                           // kEvExplosion: the mob that set it off
		std::uint32_t reserved[3];
	};
	static_assert(sizeof(McEvent) == 48);
	static_assert(kOffEventRing + kEventRingDataOff + sizeof(McEvent) * kEventRingEntries <= kOffWorldEntities);

	// ---- world entities @0x1E000 (MC -> host, seqlock) ---------------------------------------
	// Minecraft things the host draws itself each frame (arrows, dropped items, block cracks) + the
	// block outline.
	inline constexpr std::uint32_t kMaxWorldEntities = 160;

	enum WorldEntityKind : std::uint32_t
	{
		kWeArrow = 1,    // uv[0]: the arrow's item icon
		kWeItem = 2,     // dropped/thrown item: a flat sprite (uv[0]) turning about the vertical
		kWeTrident = 3,  // uv[0]: the trident's item icon
		kWeBlock = 4,    // dropped block item: a spinning cube of side `scale`, uv[0..2] = side, top, bottom
		kWeCrack = 5,    // block-breaking cracks over the box at (x, y, z) of size ext, uv[0] = crack stage
		kWeShadow = 6,   // a player's or mob's feet at (x, y, z), `scale` wide: its soft contact shadow
	};

	struct WorldEntity
	{
		std::uint32_t kind;        // WorldEntityKind
		std::uint32_t id;          // MC entity id (stable while it exists)
		float         x, y, z;     // MC coords (interpolated at MC's render time)
		float         yaw, pitch;  // MC degrees
		float         scale;
		float         ext[3];      // kWeCrack: box size
		float         uv[3][4];    // atlas rects {u0, v0, u1, v1}
		std::uint32_t tint;        // RGBA8 multiplier for the top face (grass, leaves); 0 = none
	};
	static_assert(sizeof(WorldEntity) == 96);

	struct WorldEntities
	{
		std::uint32_t seq;
		std::uint32_t count;
		std::uint32_t hasSelection;           // draw an outline around the targeted block
		float         selMin[3], selMax[3];   // MC coords
		std::uint8_t  pad[0x40 - 36];
		WorldEntity   entities[kMaxWorldEntities];
	};
	static_assert(sizeof(WorldEntities) == 0x40 + sizeof(WorldEntity) * kMaxWorldEntities);
	static_assert(kOffWorldEntities + sizeof(WorldEntities) <= kOffHostDebug);

	// ---- host debug @0x22000 (host -> MC, seqlock) --------------------------------------------
	// (#7) what Minecraft's debug screen (F3) shows about the host, a few times a second. Two
	// seqlocked parts, one per writer: client.dll's HostDebug at the region's start and server.dll's
	// HostDebugServer at kHostDebugServerOff (what's under the crosshair: npc health isn't networked
	// to the client). Strings are UTF-8, NUL-terminated (truncated).
	inline constexpr std::uint64_t kHostDebugServerOff = 0x800;  // from kOffHostDebug

	enum HostDebugFlags : std::uint32_t
	{
		kDebugPuppet = 1u << 0,          // Minecraft drives the host's player
		kDebugMinecraftInput = 1u << 1,  // keys and mouse go to Minecraft
		kDebugMinecraftHud = 1u << 2,    // Minecraft's overlay is shown
	};

	struct HostDebug
	{
		std::uint32_t seq;
		std::uint32_t flags;              // HostDebugFlags
		char          map[64];            // the host's map; empty: none loaded
		char          chapter[8];         // the map's chapter ("9a"), from the game's cfg/chapter*.cfg; empty: none
		char          chapterTitle[64];   // the chapter's title in the player's language
		float         origin[3];          // the player's feet, host units (Source: z up)
		float         angles[3];          // the player's view: pitch, yaw, roll (host degrees)
		float         fps;                // the host's frames over the last second
		float         worstFrameMs;       // the longest of them
		float         overlayMs;          // what drawing Minecraft's overlay cost a frame, on average over them
		std::int32_t  slot;               // the map's slot in Minecraft's world (1024 blocks along x each)
		std::uint32_t collisionEpoch;     // as in HostState
		std::uint32_t inputPending;       // input ring entries Minecraft hasn't read yet
		std::uint32_t eventPending;       // event ring entries the host hasn't read yet
		std::uint32_t lightEmitters;      // Minecraft's light-emitting blocks the host knows of
		std::uint64_t collisionPending;   // collision ring bytes Minecraft hasn't read yet
		std::uint64_t renderPending;      // render ring bytes the host hasn't read yet
		std::uint32_t lights;             // lights made of those emitters around the player
		std::uint32_t shadowedLights;     // of those, the shadowed point lights (the rest light only characters)
	};
	static_assert(sizeof(HostDebug) == 0xE0);

	struct HostDebugServer
	{
		std::uint32_t seq;
		std::uint32_t entityCount;   // the host's edicts in use
		std::int32_t  targetIndex;   // the entity under the crosshair: its entity index (0: it has none)
		std::int32_t  health;        // its health and maximum health
		std::int32_t  maxHealth;
		float         distance;      // from the player's eye, host units
		std::int32_t  relation;      // how a character feels about the player: Source's Disposition_t (1 hate,
		                             // 2 fear, 3 like, 4 neutral); 0: not a character
		std::int32_t  npcState;      // Source's NPC_STATE (1 idle, 2 alert, 3 combat, 4 script, 5 play dead,
		                             // 6 held by a barnacle, 7 dead); 0: not an npc
		char          targetClass[48];  // empty: nothing under the crosshair (the world doesn't count)
		char          targetName[48];   // its map name; empty: none
		char          schedule[48];     // what an npc is doing (its AI schedule)
	};
	static_assert(sizeof(HostDebugServer) == 0xB0);
	static_assert(sizeof(HostDebug) <= kHostDebugServerOff);
	static_assert(kHostDebugServerOff + sizeof(HostDebugServer) <= kHostDebugBytes);

	// ---- weapon table @0x23000 (host -> MC, seqlock) ------------------------------------------
	// (#12) the host's weapons the player owns, which Minecraft shows as items (McState::heldWeapon).
	// The host's inventory is the authority: Minecraft adds the items it lacks and removes the ones
	// the host no longer lists, and the host's player has out whichever weapon Minecraft's hand holds.

	// Which weapon (Half-Life 2 and its episodes give the player these; Minecraft has one item each).
	enum WeaponId : std::uint32_t
	{
		kHostWeaponNone = 0,
		kHostWeaponCrowbar = 1,     // weapon_crowbar
		kHostWeaponPhyscannon = 2,  // weapon_physcannon (the gravity gun, also supercharged)
		kHostWeaponPistol = 3,      // weapon_pistol
		kHostWeapon357 = 4,         // weapon_357
		kHostWeaponSmg1 = 5,        // weapon_smg1
		kHostWeaponAr2 = 6,         // weapon_ar2
		kHostWeaponShotgun = 7,     // weapon_shotgun
		kHostWeaponCrossbow = 8,    // weapon_crossbow
		kHostWeaponFrag = 9,        // weapon_frag
		kHostWeaponRpg = 10,        // weapon_rpg
		kHostWeaponBugbait = 11,    // weapon_bugbait
	};

	enum WeaponTableFlags : std::uint32_t
	{
		kWeaponTableLive = 1u << 0,  // the host's player is in a map and alive: the list is its inventory
	};

	enum WeaponRecordFlags : std::uint32_t
	{
		kWeaponRecordSupercharged = 1u << 0,  // the gravity gun, supercharged (the Citadel)
	};

	inline constexpr std::uint32_t kMaxHostWeapons = 48;  // Source's MAX_WEAPONS

	struct WeaponRecord
	{
		std::uint32_t id;        // WeaponId
		std::int32_t  clip;      // rounds in the clip; -1: no clip (it fires from the reserve, or uses no ammo)
		std::int32_t  maxClip;   // the clip's size; -1: no clip
		std::int32_t  ammo;      // primary rounds in reserve; -1: it uses no ammo (crowbar, gravity gun)
		std::int32_t  maxAmmo;   // the most primary rounds the player can carry
		std::int32_t  ammo2;     // secondary rounds (SMG grenades, AR2 balls); -1: none
		std::int32_t  maxAmmo2;
		std::uint32_t flags;     // WeaponRecordFlags
	};
	static_assert(sizeof(WeaponRecord) == 32);

	struct WeaponTable
	{
		std::uint32_t seq;
		std::uint32_t flags;   // WeaponTableFlags
		std::uint32_t count;
		std::uint32_t active;  // WeaponId the host's player has out right now (0: none)
		std::uint8_t  pad[0x40 - 16];
		WeaponRecord  weapons[kMaxHostWeapons];
	};
	static_assert(sizeof(WeaponTable) == 0x40 + sizeof(WeaponRecord) * kMaxHostWeapons);
	static_assert(sizeof(WeaponTable) <= kWeaponTableBytes);

	// ---- mob table @0x24000 (MC -> host, seqlock) ---------------------------------------------
	// (#13) Minecraft's mobs near the player, which the host's characters see and fight: its monsters
	// and the player's pets (no animals or villagers), nearest first. Written once per Minecraft tick.
	inline constexpr std::uint32_t kMaxMobs = 256;

	enum MobFlags : std::uint32_t
	{
		kMobHostile = 1u << 0,  // a monster: the host's enemies of monsters fight it
		kMobPet = 1u << 1,      // tamed by the player: the host's characters see it as the player's ally
		kMobUndead = 1u << 2,   // zombies, skeletons, ...
	};

	// kInHurtMob's attacker when the host's own player landed the hit (an actor id is never this).
	inline constexpr std::uint32_t kMobAttackerPlayer = 0xFFFFFFFFu;

	struct MobRecord
	{
		std::uint32_t id;         // the Minecraft entity id, stable while the mob exists
		std::uint32_t flags;      // MobFlags
		float         x, y, z;    // feet, MC coords
		float         yaw;        // MC degrees
		float         width;      // blocks
		float         height;     // blocks
		float         health;     // Minecraft points
		float         maxHealth;
		std::uint32_t pad[2];
	};
	static_assert(sizeof(MobRecord) == 48);

	struct MobTable
	{
		std::uint32_t seq;
		std::uint32_t count;
		std::uint8_t  pad[0x40 - 8];
		MobRecord     mobs[kMaxMobs];
	};
	static_assert(sizeof(MobTable) == 0x40 + sizeof(MobRecord) * kMaxMobs);
	static_assert(sizeof(MobTable) <= kMobTableBytes);
	static_assert(kOffHostDebug + kHostDebugBytes <= kOffWeaponTable);
	static_assert(kOffWeaponTable + kWeaponTableBytes <= kOffMobTable);
	static_assert(kOffMobTable + kMobTableBytes <= kOffCollisionRing);
	// 0x29000-0x30FFF: kept for the world group (C). HostState's 0x40-0x7F for the puppet group (A), 0x80-0xBF for C.

	// ---- render ring (MC -> host) ---------------------------------------------------------------
	// Byte ring like the collision ring. Minecraft ships its own block meshes (built by Minecraft's
	// block renderer: models, tint, AO, lighting) and its block atlas; the host draws them in its own
	// frame so blocks stay locked to the world and are hidden by the host's geometry.
	inline constexpr std::uint64_t kRenRingHeadOff = 0x00;
	inline constexpr std::uint64_t kRenRingTailOff = 0x40;
	inline constexpr std::uint64_t kRenRingDataOff = 0x80;
	inline constexpr std::uint64_t kRenRingDataBytes = kRenderRingBytes - kRenRingDataOff;

	enum RenType : std::uint32_t
	{
		kRenPad = 0,
		kRenAtlas = 1,        // RenAtlas + RGBA8 pixels (w * h * 4), top row first
		kRenSection = 2,      // RenSection + RenVertex[vertexCount] (triangle list); 0 vertices = remove
		kRenClearAll = 3,     // drop every section (world change)
		kRenTexture = 4,      // RenTexture + RGBA8 pixels: an entity texture (player skin, armour, ...)
		kRenAvatar = 5,       // RenAvatar + RenBatch[batchCount] + RenVertex[vertexCount]: the player's
		                      // model this frame; 0 batches = not shown (first person)
		kRenScene = 6,        // RenScene + RenBatch[batchCount] + RenVertex[vertexCount]: every other
		                      // entity and all particles this frame, relative to RenScene's origin
		kRenAtlasRegion = 7,  // RenAtlasRegion + RGBA8 pixels: an animated sprite's current frame
		kRenLights = 8,       // RenLights + RenLight[count]: a section's light-emitting blocks (sent
		                      // after its kRenSection; 0 = none)
		kRenRagdoll = 9,      // RenAvatar + RenBatch[] + RenVertex[]: the player's body standing still,
		                      // relative to the feet and facing +Z, split into its parts (RenBatch
		                      // flags bits 8-11: RagdollPart). Sent about once a second while alive,
		                      // for a host that hangs the parts on a ragdoll when the player dies.
		kRenSolids = 10,      // RenSolids + 512-byte bitset (bit x + 16z + 256y): which blocks of a
		                      // section the host's characters collide with (sent after its kRenSection; 0 = none)
	};

	struct RenSolids
	{
		std::int32_t  sx, sy, sz;  // section coords, as in RenSection
		std::uint32_t count;       // solid blocks (0: none, and no bitset follows)
	};

	enum RagdollPart : std::uint32_t
	{
		kPartNone = 0,
		kPartHead = 1,
		kPartBody = 2,
		kPartRightArm = 3,
		kPartLeftArm = 4,
		kPartRightLeg = 5,
		kPartLeftLeg = 6,
		kPartCount = 7,
	};

	struct RenLights
	{
		std::int32_t  sx, sy, sz;  // section coords, as in RenSection
		std::uint32_t count;
	};

	enum LightKind : std::uint8_t
	{
		kLightSteady = 0,
		kLightFlame = 1,  // torches, fire, campfires, candles: flicker
		kLightLava = 2,   // lava, magma: a slow glow
	};

	enum BlockHazard : std::uint8_t
	{
		kHazardNone = 0,
		kHazardFire = 1,   // fire, soul fire, campfires: burns what stands in it
		kHazardLava = 2,   // lava: burns hard
		kHazardMagma = 3,  // magma block: hurts what stands on top of it
	};

	struct RenLight
	{
		std::uint8_t  x, y, z;  // block within the section
		std::uint8_t  level;    // Minecraft light emission, 1-15
		std::uint32_t color;    // RGB8 (r low byte); top byte: LightKind in bits 0-3, BlockHazard in 4-7
	};
	static_assert(sizeof(RenLight) == 8);

	struct RenAtlasRegion
	{
		std::uint32_t x, y, width, height;  // pixels in the combined atlas (kRenAtlas)
	};

	struct RenScene
	{
		double        originX, originY, originZ;  // MC block the positions are relative to
		std::uint32_t batchCount;
		std::uint32_t vertexCount;
	};

	struct RenTexture
	{
		std::uint32_t id;  // 1+, referenced by RenBatch::texture
		std::uint32_t width, height;
		std::uint32_t pad;
	};

	// The player as Minecraft's own entity renderer draws it (skin, armour, held items, cape),
	// posed and animated, with positions in blocks relative to the player's feet: the host puts
	// it at its own interpolated feet position, the one its camera follows.
	struct RenAvatar
	{
		std::uint32_t batchCount;
		std::uint32_t vertexCount;
	};

	struct RenBatch
	{
		std::uint32_t texture;  // 0: the block/item atlas, else a RenTexture id
		std::uint32_t first;    // first vertex
		std::uint32_t count;    // vertices (multiple of 3)
		std::uint32_t flags;    // bit0: translucent (blended, after the solid pass)
	};

	struct RenAtlas
	{
		std::uint32_t width, height;
	};

	struct RenSection
	{
		std::int32_t  sx, sy, sz;   // section coords (16-block cubes)
		std::uint32_t vertexCount;  // multiple of 3
	};

	struct RenVertex
	{
		float         x, y, z;  // MC coords relative to the section origin (sx*16, sy*16, sz*16)
		float         u, v;     // atlas UV
		std::uint32_t color;    // RGBA8 (tint * ambient occlusion; Minecraft's fixed face shading is left out)
		std::uint32_t light;    // low byte: block light 0-15, next byte: sky light 0-15
		std::uint32_t flags;    // bit0: cutout (alpha test), bit1: translucent,
		                        // bits 4-6: face normal as MC Direction ordinal + 1 (0 = none: lit without a normal)
	};
	static_assert(sizeof(RenVertex) == 32);

	struct InputEvent
	{
		std::uint16_t type;
		std::uint16_t code;
		std::int32_t  a;
		std::int32_t  b;
		std::int32_t  c;
	};
	static_assert(sizeof(InputEvent) == 16);

	// ---- collision ring @0x40000 (the host produces, MC consumes) ----------------------------
	// Byte ring. Every message starts 8-byte aligned with {u32 type, u32 payloadBytes}.
	// A kColPad message means "skip to the start of the ring".
	inline constexpr std::uint64_t kColRingHeadOff = 0x00;  // u64 total bytes written
	inline constexpr std::uint64_t kColRingTailOff = 0x40;  // u64 total bytes consumed
	inline constexpr std::uint64_t kColRingDataOff = 0x80;
	inline constexpr std::uint64_t kColRingDataBytes = kCollisionRingBytes - kColRingDataOff;

	enum ColType : std::uint32_t
	{
		kColPad = 0,
		kColClear = 1,   // payload: u32 epoch
		kColRegion = 2,  // payload: ColRegion + ColBlock[count]
		kColTris = 3,    // payload: ColRegion (count = triangles) + ColTri[count]; sent before kColRegion
	};

	// Exact host collision triangle (MC space) for the player's smooth collider.
	enum ColTriFlags : std::uint32_t
	{
		kTriStairHelper = 1u << 0,  // an invisible stair ramp: walkable, never a wall
	};

	struct ColTri
	{
		float         v[9];
		std::uint32_t flags;  // ColTriFlags
	};
	static_assert(sizeof(ColTri) == 40);

	struct ColMsgHeader
	{
		std::uint32_t type;
		std::uint32_t payloadBytes;
	};

	// Replaces all host collision inside the inclusive block box [min, max].
	struct ColRegion
	{
		std::int32_t  minX, minY, minZ;
		std::int32_t  maxX, maxY, maxZ;
		std::uint32_t epoch;
		std::uint32_t count;
	};
	static_assert(sizeof(ColRegion) == 32);

	// One block's worth of host collision as an 8x8x8 occupancy mask.
	// bits[y] bit (z * 8 + x) is sub-voxel (x, y, z), each 1/8 block, in MC axes.
	struct ColBlock
	{
		std::int32_t  x, y, z;
		std::uint32_t pad;
		std::uint64_t bits[8];
	};
	static_assert(sizeof(ColBlock) == 80);
}

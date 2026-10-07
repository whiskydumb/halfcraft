package dev.halfcraft.link;

/**
 * Mirror of protocol/halfcraft_protocol.h. Keep the two in sync.
 */
public final class Proto {
	private Proto() {
	}

	public static final int MAGIC = 0x464C4148; // "HALF"
	public static final int VERSION = 3;
	public static final String MAPPING_NAME = "Local\\HalfCraft_v1";

	public static final long OFF_HEADER = 0x0;
	public static final long OFF_HOST_STATE = 0x100;
	public static final long OFF_MC_STATE = 0x200;
	public static final long OFF_WATER_GRID = 0x400;
	public static final int WATER_GRID_SIZE = 16;
	public static final long WG_SEQ = 0x0, WG_ORIGIN_X = 0x4, WG_ORIGIN_Z = 0x8, WG_WORLD_ID = 0xC, WG_SURFACE = 0x10;
	public static final long OFF_OVERLAY_CTL = 0x300;
	public static final long OFF_OVERLAY_SLOT_HDR = 0x340;
	public static final long OFF_INPUT_RING = 0x1000;
	public static final long OFF_COLLISION_RING = 0x40000;
	public static final long COLLISION_RING_BYTES = 32L << 20;
	public static final long OFF_OVERLAY_PIXELS = OFF_COLLISION_RING + COLLISION_RING_BYTES;
	public static final int MAX_OVERLAY_W = 3840;
	public static final int MAX_OVERLAY_H = 2160;
	public static final long OVERLAY_SLOT_BYTES = (long) MAX_OVERLAY_W * MAX_OVERLAY_H * 4;
	public static final int OVERLAY_SLOTS = 3;
	public static final long OFF_ACTOR_TABLE = 0x12000;
	public static final long OFF_EVENT_RING = 0x17000;
	public static final long OFF_WORLD_ENTITIES = 0x1E000;
	public static final long OFF_HOST_DEBUG = 0x22000;
	public static final long HOST_DEBUG_BYTES = 0x1000;
	public static final long OFF_WEAPON_TABLE = 0x23000;
	public static final long WEAPON_TABLE_BYTES = 0x1000;
	public static final long OFF_MOB_TABLE = 0x24000;
	public static final long MOB_TABLE_BYTES = 0x5000;
	public static final long OFF_RENDER_RING = OFF_OVERLAY_PIXELS + OVERLAY_SLOT_BYTES * OVERLAY_SLOTS;
	public static final long RENDER_RING_BYTES = 64L << 20;
	public static final long MAPPING_BYTES = OFF_RENDER_RING + RENDER_RING_BYTES;

	// Input types added in v5
	public static final int IN_HURT = 7;
	public static final int IN_OPEN_MENU = 8;
	public static final int IN_HEAL = 9;
	public static final int IN_CHECKPOINT = 10;
	public static final int IN_RESTORE = 11;
	public static final int IN_STRING = 12;
	public static final int IN_HURT_MOB = 13; // (#13)
	public static final int IN_PUSH = 22; // a/b/c = blocks per second * 1000 (HostPush)
	public static final int PUSH_REPEAT_MS = 200;
	public static final int PUSH_STALE_MS = 500;
	// 23: kept for the puppet group (A), 26-27 for the world group (C)
	public static final int IN_IMPULSE = 23; // a/b/c = blocks per second * 1000, a shove that's over at once (HostPush)
	public static final int IN_HURT_FROM = 24; // where the next IN_HURT came from: a/b/c = x/y/z (float bits)

	// String channels (IN_STRING)
	public static final int STR_COMMAND = 1; // (#11)
	public static final int STR_SCREENSHOT = 2; // (#6) "ok <request> <width> <height> <path>" or "fail <request> <reason>" (ScreenshotAnswer)
	public static final int STRING_CHANNEL_MASK = 0xFF;
	public static final int STRING_BYTES_SHIFT = 8;
	public static final int STRING_END = 1 << 15;
	public static final int STRING_PIECE_BYTES = 12;
	public static final int HEAL_HEALTH = 0;
	public static final int HEAL_ARMOR = 1;
	public static final int HURT_MELEE = 0;
	public static final int HURT_PROJECTILE = 1;
	public static final int HURT_MAGIC = 2;
	public static final int HURT_OTHER = 3;
	public static final int HURT_BLAST = 4;
	public static final int HURT_FIRE = 5;
	public static final int HURT_CRUSH = 6;
	public static final int HURT_POWER_ATTACK = 2;

	// Actor table (relative to OFF_ACTOR_TABLE)
	public static final int MAX_ACTORS = 256;
	public static final long AT_SEQ = 0x00;
	public static final long AT_COUNT = 0x04;
	public static final long AT_RECORDS = 0x40;
	public static final long ACTOR_RECORD_BYTES = 64;
	public static final int ACTOR_HOSTILE = 1;
	public static final int ACTOR_DEAD = 1 << 1;
	public static final int ACTOR_ESSENTIAL = 1 << 2;
	public static final int ACTOR_IN_COMBAT = 1 << 3;

	// Event ring (relative to OFF_EVENT_RING)
	public static final int EVENT_RING_ENTRIES = 512;
	public static final long ER_HEAD = 0x00;
	public static final long ER_TAIL = 0x40;
	public static final long ER_DATA = 0x80;
	public static final long EVENT_BYTES = 48;
	public static final int EV_HIT_ACTOR = 1;
	public static final int EV_PLAYER_DIED = 2;
	public static final int EV_EXPLOSION = 3; // attackerId = the mob that set it off (0: nobody)
	public static final int EV_ARROW_STUCK = 4;
	public static final int EV_SCREENSHOT = 5; // (#6) actorId = the request number (1+)
	// 12-13: kept for the puppet group (A), 16-17 for the world group (C)
	// 6-7: kept for #12 (the host's weapons), 8-9 for #13 (Minecraft's mobs)
	public static final int HIT_CRITICAL = 1;
	public static final int HIT_PROJECTILE = 1 << 1;
	public static final int HIT_SWEEP = 1 << 2;
	public static final int HIT_FIRE = 1 << 3;
	public static final int WEAPON_UNARMED = 0;
	public static final int WEAPON_BLADE = 1;
	public static final int WEAPON_AXE = 2;
	public static final int WEAPON_BLUNT = 3;
	public static final int WEAPON_PIERCE = 4;
	public static final int WEAPON_ARROW = 5;

	// World entities (relative to OFF_WORLD_ENTITIES)
	public static final int MAX_WORLD_ENTITIES = 160;
	public static final long WE_SEQ = 0x00;
	public static final long WE_COUNT = 0x04;
	public static final long WE_HAS_SELECTION = 0x08;
	public static final long WE_SEL_MIN = 0x0C;
	public static final long WE_SEL_MAX = 0x18;
	public static final long WE_RECORDS = 0x40;
	public static final long WORLD_ENTITY_BYTES = 96;
	public static final int WE_ARROW = 1;
	public static final int WE_ITEM = 2;
	public static final int WE_TRIDENT = 3;
	public static final int WE_BLOCK = 4;
	public static final int WE_CRACK = 5;
	public static final int WE_SHADOW = 6;

	// Render ring (relative to OFF_RENDER_RING)
	public static final long RR_HEAD = 0x00;
	public static final long RR_TAIL = 0x40;
	public static final long RR_DATA = 0x80;
	public static final long RR_DATA_BYTES = RENDER_RING_BYTES - RR_DATA;
	public static final int REN_PAD = 0;
	public static final int REN_ATLAS = 1;
	public static final int REN_SECTION = 2;
	public static final int REN_CLEAR_ALL = 3;
	public static final int REN_TEXTURE = 4;
	public static final int REN_AVATAR = 5;
	public static final int REN_SCENE = 6;
	public static final int REN_ATLAS_REGION = 7;
	public static final int REN_LIGHTS = 8;
	public static final int REN_RAGDOLL = 9;
	public static final int REN_SOLIDS = 10;
	public static final int PART_HEAD = 1, PART_BODY = 2, PART_RIGHT_ARM = 3, PART_LEFT_ARM = 4, PART_RIGHT_LEG = 5, PART_LEFT_LEG = 6;
	public static final int LIGHT_STEADY = 0, LIGHT_FLAME = 1, LIGHT_LAVA = 2;
	public static final int REN_VERTEX_BYTES = 32;

	// Header
	public static final long H_MAGIC = 0x00;
	public static final long H_VERSION = 0x04;
	public static final long H_HOST_PID = 0x08;
	public static final long H_MC_PID = 0x0C;
	public static final long H_HOST_HEARTBEAT = 0x10;
	public static final long H_MC_HEARTBEAT = 0x18;
	public static final long H_HOST_SESSION = 0x20;

	// HostState (relative to OFF_HOST_STATE)
	public static final long HS_SEQ = 0x00;
	public static final long HS_FLAGS = 0x04;
	public static final long HS_WORLD_ID = 0x08;
	public static final long HS_COLLISION_EPOCH = 0x0C;
	public static final long HS_POS_X = 0x10;
	public static final long HS_POS_Y = 0x18;
	public static final long HS_POS_Z = 0x20;
	public static final long HS_YAW = 0x28;
	public static final long HS_PITCH = 0x2C;
	public static final long HS_TELEPORT_SEQ = 0x30;
	public static final long HS_VIEWPORT_W = 0x34;
	public static final long HS_VIEWPORT_H = 0x38;
	public static final long HS_GAME_HOUR = 0x3C;
	public static final long HS_SEAT_YAW = 0x40;
	public static final long HS_SPEED_FACTOR = 0x44;

	public static final int HOST_IN_GAME = 1;
	public static final int HOST_MENU_OPEN = 1 << 1;
	public static final int HOST_LOADING = 1 << 2;
	public static final int HOST_SEATED = 1 << 3;
	public static final int HOST_TAKEOVER = 1 << 7;

	// McState (relative to OFF_MC_STATE)
	public static final long MS_SEQ = 0x00;
	public static final long MS_FLAGS = 0x04;
	public static final long MS_X = 0x08;
	public static final long MS_Y = 0x10;
	public static final long MS_Z = 0x18;
	public static final long MS_YAW = 0x20;
	public static final long MS_PITCH = 0x24;
	public static final long MS_EYE_HEIGHT = 0x28;
	public static final long MS_SENSITIVITY = 0x2C;
	public static final long MS_TELEPORT_ACK = 0x30;
	public static final long MS_GUI_SCALE = 0x34;
	public static final long MS_FRAME_COUNTER = 0x38;
	public static final long MS_FOV = 0x40;
	public static final long MS_BOB_PHASE = 0x44;
	public static final long MS_BOB_AMOUNT = 0x48;
	public static final long MS_EYE_X = 0x50;
	public static final long MS_EYE_Y = 0x58;
	public static final long MS_EYE_Z = 0x60;
	public static final long MS_TICK_QPC = 0x68;
	public static final long MS_PREV_X = 0x70;
	public static final long MS_CUR_X = 0x88;
	public static final long MS_EYE_HEIGHT_O = 0xA0;
	public static final long MS_EYE_HEIGHT_T = 0xA4;
	public static final long MS_WALK_O = 0xA8;
	public static final long MS_WALK = 0xAC;
	public static final long MS_BOB_O = 0xB0;
	public static final long MS_BOB = 0xB4;
	public static final long MS_TICK_MS = 0xB8;
	public static final long MS_CAMERA_MODE = 0xC0;
	public static final long MS_CAMERA_DISTANCE = 0xC4;
	public static final long MS_HEALTH = 0xC8;
	public static final long MS_MAX_HEALTH = 0xCC;
	public static final long MS_ABSORPTION = 0xD0;
	public static final long MS_HELD_WEAPON = 0xD8; // (#12)
	public static final long MS_TELEPORT_COUNT = 0xE0; // Minecraft's own teleports (HostTeleports)

	public static final int MC_IN_WORLD = 1;
	public static final int MC_SCREEN_OPEN = 1 << 1;
	public static final int MC_ON_GROUND = 1 << 2;
	public static final int MC_SNEAKING = 1 << 3;
	public static final int MC_SPRINTING = 1 << 4;
	public static final int MC_DEAD = 1 << 5;
	public static final int MC_SWIMMING = 1 << 6;
	public static final int MC_FLYING = 1 << 7;
	public static final int MC_SLEEPING = 1 << 8;

	// Overlay
	public static final long OC_STATE = 0x00;
	public static final long OC_FRAMES_PUBLISHED = 0x08;
	public static final int OVERLAY_DIRTY = 1 << 2;
	public static final long SLOT_HDR_SIZE = 0x40;
	public static final long SH_WIDTH = 0x00;
	public static final long SH_HEIGHT = 0x04;
	public static final long SH_FLAGS = 0x08;
	public static final long SH_FRAME_ID = 0x10;

	// Input ring (relative to OFF_INPUT_RING)
	public static final int INPUT_RING_ENTRIES = 4096;
	public static final long IR_HEAD = 0x00;
	public static final long IR_TAIL = 0x40;
	public static final long IR_DATA = 0x80;
	public static final int IN_KEY = 1;
	public static final int IN_MOUSE_BUTTON = 2;
	public static final int IN_SCROLL = 3;
	public static final int IN_CURSOR = 4;
	public static final int IN_TEXT = 5;
	public static final int IN_RELEASE_ALL = 6;

	// Collision ring (relative to OFF_COLLISION_RING)
	public static final long CR_HEAD = 0x00;
	public static final long CR_TAIL = 0x40;
	public static final long CR_DATA = 0x80;
	public static final long CR_DATA_BYTES = COLLISION_RING_BYTES - CR_DATA;
	public static final int COL_PAD = 0;
	public static final int COL_CLEAR = 1;
	public static final int COL_REGION = 2;
	public static final int COL_TRIS = 3;
	public static final int COL_TRI_BYTES = 40;
	public static final int TRI_STAIR_HELPER = 1;
	public static final int COL_REGION_HEADER_BYTES = 32;
	public static final int COL_BLOCK_BYTES = 80;

	// Host debug (relative to OFF_HOST_DEBUG): (#7) client.dll's HostDebug, then server.dll's HostDebugServer at HD_SERVER
	public static final long HD_SEQ = 0x00;
	public static final long HD_FLAGS = 0x04;
	public static final long HD_MAP = 0x08;
	public static final int HD_MAP_BYTES = 64;
	public static final long HD_CHAPTER = 0x48;
	public static final int HD_CHAPTER_BYTES = 8;
	public static final long HD_CHAPTER_TITLE = 0x50;
	public static final int HD_CHAPTER_TITLE_BYTES = 64;
	public static final long HD_ORIGIN = 0x90;
	public static final long HD_ANGLES = 0x9C;
	public static final long HD_FPS = 0xA8;
	public static final long HD_WORST_FRAME_MS = 0xAC;
	public static final long HD_OVERLAY_MS = 0xB0;
	public static final long HD_SLOT = 0xB4;
	public static final long HD_COLLISION_EPOCH = 0xB8;
	public static final long HD_INPUT_PENDING = 0xBC;
	public static final long HD_EVENT_PENDING = 0xC0;
	public static final long HD_LIGHT_EMITTERS = 0xC4;
	public static final long HD_COLLISION_PENDING = 0xC8;
	public static final long HD_RENDER_PENDING = 0xD0;
	public static final long HD_LIGHTS = 0xD8;
	public static final long HD_SHADOWED_LIGHTS = 0xDC;
	public static final long HD_GRID_Z = 0xE0;
	public static final long HD_BYTES = 0xE8;
	public static final int HD_PUPPET = 1;
	public static final int HD_MINECRAFT_INPUT = 1 << 1;
	public static final int HD_MINECRAFT_HUD = 1 << 2;
	public static final long HD_SERVER = 0x800;
	public static final long HDS_SEQ = 0x00;
	public static final long HDS_ENTITY_COUNT = 0x04;
	public static final long HDS_TARGET_INDEX = 0x08;
	public static final long HDS_HEALTH = 0x0C;
	public static final long HDS_MAX_HEALTH = 0x10;
	public static final long HDS_DISTANCE = 0x14;
	public static final long HDS_RELATION = 0x18;
	public static final long HDS_NPC_STATE = 0x1C;
	public static final long HDS_TARGET_CLASS = 0x20;
	public static final long HDS_TARGET_NAME = 0x50;
	public static final long HDS_SCHEDULE = 0x80;
	public static final int HDS_TEXT_BYTES = 48;
	public static final long HDS_BYTES = 0xB0;

	// Weapon table (relative to OFF_WEAPON_TABLE): (#12)
	public static final long WT_SEQ = 0x00;
	public static final long WT_FLAGS = 0x04;
	public static final long WT_COUNT = 0x08;
	public static final long WT_ACTIVE = 0x0C;
	public static final long WT_RECORDS = 0x40;
	public static final long WT_RECORD_BYTES = 32;
	public static final int WT_MAX_WEAPONS = 48;
	public static final int WT_LIVE = 1;
	// WeaponRecord (relative to the record)
	public static final long WT_R_ID = 0x00;
	public static final long WT_R_CLIP = 0x04;
	public static final long WT_R_MAX_CLIP = 0x08;
	public static final long WT_R_AMMO = 0x0C;
	public static final long WT_R_MAX_AMMO = 0x10;
	public static final long WT_R_AMMO2 = 0x14;
	public static final long WT_R_MAX_AMMO2 = 0x18;
	public static final long WT_R_FLAGS = 0x1C;
	public static final int WT_R_SUPERCHARGED = 1;
	// WeaponId (WeaponRecord.id, WT_ACTIVE, MS_HELD_WEAPON)
	public static final int HOST_WEAPON_NONE = 0;
	public static final int HOST_WEAPON_CROWBAR = 1;
	public static final int HOST_WEAPON_PHYSCANNON = 2;
	public static final int HOST_WEAPON_PISTOL = 3;
	public static final int HOST_WEAPON_357 = 4;
	public static final int HOST_WEAPON_SMG1 = 5;
	public static final int HOST_WEAPON_AR2 = 6;
	public static final int HOST_WEAPON_SHOTGUN = 7;
	public static final int HOST_WEAPON_CROSSBOW = 8;
	public static final int HOST_WEAPON_FRAG = 9;
	public static final int HOST_WEAPON_RPG = 10;
	public static final int HOST_WEAPON_BUGBAIT = 11;

	// Mob table (relative to OFF_MOB_TABLE): (#13)
	public static final long MT_SEQ = 0x00;
	public static final long MT_COUNT = 0x04;
	public static final long MT_RECORDS = 0x40;
	public static final int MAX_MOBS = 256;
	public static final long MOB_RECORD_BYTES = 48;
	public static final long MR_ID = 0x00, MR_FLAGS = 0x04, MR_X = 0x08, MR_Y = 0x0C, MR_Z = 0x10, MR_YAW = 0x14, MR_WIDTH = 0x18,
		MR_HEIGHT = 0x1C, MR_HEALTH = 0x20, MR_MAX_HEALTH = 0x24;
	public static final int MOB_HOSTILE = 1;
	public static final int MOB_PET = 1 << 1;
	public static final int MOB_UNDEAD = 1 << 2;
	public static final int MOB_ATTACKER_PLAYER = 0xFFFFFFFF;

	// Water probes (relative to OFF_WATER_PROBES): Minecraft's WaterProbeRequests, then client.dll's WaterProbes at WP_ANSWERS
	public static final long OFF_WATER_PROBES = 0x29000;
	public static final long WATER_PROBES_BYTES = 0x1000;
	public static final int MAX_WATER_PROBES = 12;
	public static final int WATER_PROBE_SIZE = 8;
	public static final float WATER_PROBE_DEPTH = 6.0F; // blocks below a probe's y its search reaches
	public static final long WPR_SEQ = 0x00, WPR_COUNT = 0x04, WPR_AT = 0x08; // 3 floats (x, y, z) per request
	public static final long WP_ANSWERS = 0x100;
	public static final long WP_SEQ = 0x00, WP_COUNT = 0x04, WP_PROBES = 0x10;
	public static final long WATER_PROBE_BYTES = 0x110;
	// WaterProbe (relative to the probe)
	public static final long WP_ORIGIN_X = 0x00, WP_ORIGIN_Z = 0x04, WP_Y = 0x08, WP_SURFACE = 0x10;
	// 0x2A000-0x30FFF: kept for the world group (C)
}

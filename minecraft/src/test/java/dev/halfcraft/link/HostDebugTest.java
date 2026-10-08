package dev.halfcraft.link;

import static dev.halfcraft.link.Proto.*;
import static java.lang.foreign.ValueLayout.*;
import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

class HostDebugTest {
	private Arena arena;
	private MemorySegment shm;

	@BeforeEach
	void map() {
		arena = Arena.ofConfined();
		shm = arena.allocate(OFF_HOST_DEBUG + HOST_DEBUG_BYTES);
	}

	@AfterEach
	void unmap() {
		arena.close();
	}

	private void text(long off, String value) {
		byte[] bytes = value.getBytes(StandardCharsets.UTF_8);
		MemorySegment.copy(bytes, 0, shm, JAVA_BYTE, off, bytes.length);
		shm.set(JAVA_BYTE, off + bytes.length, (byte) 0);
	}

	/** client.dll's part as its seqlock_write leaves it. */
	private void writeClient() {
		long b = OFF_HOST_DEBUG;
		shm.set(JAVA_INT, b + HD_SEQ, 2);
		shm.set(JAVA_INT, b + HD_FLAGS, HD_PUPPET | HD_MINECRAFT_HUD);
		text(b + HD_MAP, "d2_prison_06");
		text(b + HD_CHAPTER, "9a");
		text(b + HD_CHAPTER_TITLE, "ВЗАИМОСВЯЗЬ");
		shm.set(JAVA_FLOAT, b + HD_ORIGIN + 8, -1.5F);
		shm.set(JAVA_FLOAT, b + HD_ANGLES + 4, 90.0F);
		shm.set(JAVA_FLOAT, b + HD_FPS, 143.5F);
		shm.set(JAVA_INT, b + HD_SLOT, 43);
		shm.set(JAVA_INT, b + HD_COLLISION_EPOCH, 7);
		shm.set(JAVA_LONG, b + HD_COLLISION_PENDING, 5L << 32);
		shm.set(JAVA_INT, b + HD_SHADOWED_LIGHTS, 3);
	}

	@Test
	void readsNothingBeforeHalfLifeWroteIt() {
		HostDebug out = new HostDebug();
		assertFalse(HostDebug.read(shm, out));
		assertFalse(HostDebug.read(null, out));
	}

	@Test
	void readsClientFieldsAtTheProtocolOffsets() {
		writeClient();
		HostDebug out = new HostDebug();
		assertTrue(HostDebug.read(shm, out));
		assertEquals("d2_prison_06", out.map);
		assertEquals("9a", out.chapter);
		assertEquals("ВЗАИМОСВЯЗЬ", out.chapterTitle);
		assertEquals(-1.5F, out.origin[2]);
		assertEquals(90.0F, out.angles[1]);
		assertEquals(143.5F, out.fps);
		assertEquals(43, out.slot);
		assertEquals(7, out.collisionEpoch);
		assertEquals(5L << 32, out.collisionPending);
		assertEquals(3, out.shadowedLights);
		assertTrue(out.puppet());
		assertFalse(out.minecraftInput());
		assertFalse(out.haveServer, "server.dll hasn't written its part");
		assertFalse(out.hasTarget());
	}

	@Test
	void readsServerPartAfterTheClientPart() {
		writeClient();
		long b = OFF_HOST_DEBUG + HD_SERVER;
		shm.set(JAVA_INT, b + HDS_SEQ, 4);
		shm.set(JAVA_INT, b + HDS_ENTITY_COUNT, 812);
		shm.set(JAVA_INT, b + HDS_TARGET_INDEX, 123);
		shm.set(JAVA_INT, b + HDS_HEALTH, 40);
		shm.set(JAVA_INT, b + HDS_MAX_HEALTH, 50);
		shm.set(JAVA_INT, b + HDS_NPC_STATE, 3);
		text(b + HDS_TARGET_CLASS, "npc_combine_s");
		text(b + HDS_SCHEDULE, "SCHED_RANGE_ATTACK1");
		HostDebug out = new HostDebug();
		assertTrue(HostDebug.read(shm, out));
		assertTrue(out.haveServer);
		assertTrue(out.hasTarget());
		assertEquals(812, out.server.entityCount);
		assertEquals(123, out.server.targetIndex);
		assertEquals(40, out.server.health);
		assertEquals(50, out.server.maxHealth);
		assertEquals(3, out.server.npcState);
		assertEquals("npc_combine_s", out.server.targetClass);
		assertEquals("", out.server.targetName);
		assertEquals("SCHED_RANGE_ATTACK1", out.server.schedule);
	}

	@Test
	void keepsTheLastStateWhileAWriteIsUnderway() {
		writeClient();
		HostDebug out = new HostDebug();
		assertTrue(HostDebug.read(shm, out));
		shm.set(JAVA_INT, OFF_HOST_DEBUG + HD_SEQ, 3);
		text(OFF_HOST_DEBUG + HD_MAP, "d2_prison_07");
		assertFalse(HostDebug.read(shm, out));
		assertEquals("d2_prison_06", out.map);
	}
}

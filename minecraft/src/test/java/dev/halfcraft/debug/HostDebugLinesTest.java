package dev.halfcraft.debug;

import static org.junit.jupiter.api.Assertions.assertEquals;

import dev.halfcraft.link.HostDebug;
import dev.halfcraft.link.Proto;
import java.util.List;
import org.junit.jupiter.api.Test;

class HostDebugLinesTest {
	private static HostDebug inMap() {
		HostDebug d = new HostDebug();
		d.fps = 143.6F;
		d.worstFrameMs = 9.84F;
		d.overlayMs = 0.425F;
		d.map = "d1_canals_08";
		d.chapter = "4";
		d.chapterTitle = "WATER HAZARD";
		d.origin[0] = 1234.5F;
		d.origin[1] = -67.25F;
		d.origin[2] = 8.0F;
		d.angles[0] = 12.5F;
		d.angles[1] = 90.0F;
		d.slot = 13;
		d.gridZ = 24.0F;
		return d;
	}

	@Test
	void overlaySaysItsSizeTheDivisorAndTheReadback() {
		assertEquals("Overlay: 1128x752 (1/2 of the screen), readback 1.25 ms a frame", HostDebugLines.overlay(1128, 752, 2, 1.25));
		assertEquals("Overlay: 1280x720, readback 0.40 ms a frame", HostDebugLines.overlay(1280, 720, 1, 0.4));
	}

	@Test
	void locationShowsMapChapterAndSourceCoordinates() {
		List<String> lines = HostDebugLines.location(inMap(), 13343.5, 0.2, 1.75);
		assertEquals(List.of(
			"Half-Life: 144 fps (worst frame 9.8 ms), overlay 0.43 ms",
			"Map: d1_canals_08, chapter 4: WATER HAZARD",
			"Source XYZ: 1234.50 / -67.25 / 8.00",
			"Source facing: yaw 90.0, pitch 12.5",
			"Mirror XYZ: 13343.500 / 0.200 / 1.750 (map slot 13, block grid at Source z 24)"
		), lines);
	}

	@Test
	void locationWithoutAMapSaysSo() {
		HostDebug d = new HostDebug();
		assertEquals("Map: none loaded", HostDebugLines.location(d, 0, 0, 0).get(1));
	}

	@Test
	void mapOutsideTheChaptersHasNoChapter() {
		HostDebug d = inMap();
		d.chapter = "";
		d.chapterTitle = "";
		d.map = "background01";
		assertEquals("Map: background01", HostDebugLines.location(d, 0, 0, 0).get(1));
	}

	@Test
	void linkShowsDriverRingsAndLights() {
		HostDebug d = inMap();
		d.flags = Proto.HD_PUPPET | Proto.HD_MINECRAFT_INPUT;
		d.collisionEpoch = 5;
		d.inputPending = 2;
		d.collisionPending = 3 * 1024 * 1024 / 2;
		d.renderPending = 512;
		d.lightEmitters = 12;
		d.lights = 4;
		d.shadowedLights = 3;
		d.haveServer = true;
		d.entityCount = 812;
		assertEquals(List.of(
			"Link: collision epoch 5, Minecraft drives the player, Minecraft has the input",
			"Rings: input 2, events 0, collision 1.5 MB, render 512 B",
			"Block lights: 12 emitters, 4 lights, 3 shadowed",
			"Edicts: 812"
		), HostDebugLines.link(d));
	}

	@Test
	void ringsWhoseIndicesDisagreeSaySo() {
		// all ones from the host: the ring's reader and writer disagree, its backlog means nothing
		HostDebug d = inMap();
		d.inputPending = (int) Proto.RING_OUT_OF_STEP;
		d.renderPending = Proto.RING_OUT_OF_STEP;
		d.collisionPending = 64;
		assertEquals("Rings: input out of step, events 0, collision 64 B, render out of step", HostDebugLines.link(d).get(1));
	}

	@Test
	void targetShowsAnNpcsHealthMoodAndSchedule() {
		HostDebug d = inMap();
		d.haveServer = true;
		d.targetClass = "npc_metropolice";
		d.targetName = "cop_1";
		d.targetIndex = 77;
		d.health = 30;
		d.maxHealth = 40;
		d.relation = 1;
		d.npcState = 3;
		d.schedule = "SCHED_METROPOLICE_CHASE_ENEMY";
		d.distance = 200.0F;
		assertEquals(List.of(
			"Half-Life target: npc_metropolice #77 \"cop_1\"",
			"Health: 30 / 40, hates you, combat",
			"Schedule: SCHED_METROPOLICE_CHASE_ENEMY",
			"Distance: 200 units (5.0 blocks)"
		), HostDebugLines.target(d));
	}

	@Test
	void targetOfAPropHasNoMoodOrSchedule() {
		HostDebug d = inMap();
		d.haveServer = true;
		d.targetClass = "prop_physics";
		d.health = 0;
		d.distance = 80.0F;
		assertEquals(List.of("Half-Life target: prop_physics #0", "Health: 0 / 0", "Distance: 80 units (2.0 blocks)"), HostDebugLines.target(d));
	}

	@Test
	void nothingUnderTheCrosshair() {
		assertEquals(List.of("Half-Life target: nothing"), HostDebugLines.target(inMap()));
	}

	@Test
	void serverPartLeftFromTheLastMapIsntShownInTheMenu() {
		HostDebug d = new HostDebug();
		d.haveServer = true;
		d.targetClass = "npc_zombie";
		d.entityCount = 900;
		assertEquals(List.of("Half-Life target: nothing"), HostDebugLines.target(d));
		assertEquals(3, HostDebugLines.link(d).size(), "no edict count");
	}
}

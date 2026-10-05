package dev.halfcraft;

import static org.junit.jupiter.api.Assertions.assertEquals;

import org.junit.jupiter.api.Test;

class WorldNameTest {
	@Test
	void defaultsWithoutTheProperty() {
		assertEquals("HalfCraft", WorldName.resolve(null));
		assertEquals("HalfCraft", WorldName.resolve(""));
		assertEquals("HalfCraft", WorldName.resolve("   "));
	}

	@Test
	void takesATestWorld() {
		assertEquals("HalfCraftTest", WorldName.resolve("HalfCraftTest"));
		assertEquals("Half Craft_2-test", WorldName.resolve(" Half Craft_2-test "));
	}

	@Test
	void refusesNamesThatReachOutsideSaves() {
		assertEquals("HalfCraft", WorldName.resolve("../HalfCraft2"));
		assertEquals("HalfCraft", WorldName.resolve("saves/HalfCraftTest"));
		assertEquals("HalfCraft", WorldName.resolve("C:\\worlds\\test"));
		assertEquals("HalfCraft", WorldName.resolve("."));
	}

	@Test
	void refusesOverlongNames() {
		assertEquals("HalfCraft", WorldName.resolve("x".repeat(65)));
		assertEquals("x".repeat(64), WorldName.resolve("x".repeat(64)));
	}
}

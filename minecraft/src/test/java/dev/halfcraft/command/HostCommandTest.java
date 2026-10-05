package dev.halfcraft.command;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import org.junit.jupiter.api.Test;

class HostCommandTest {
	@Test
	void keepsACommandAsItIs() {
		assertEquals("give @s minecraft:trident[enchantments={loyalty:3}] 1", HostCommand.normalize("give @s minecraft:trident[enchantments={loyalty:3}] 1"));
	}

	@Test
	void dropsTheSlashChatNeeds() {
		assertEquals("time set midnight", HostCommand.normalize("/time set midnight"));
		assertEquals("kill @s", HostCommand.normalize("  / kill @s "));
	}

	@Test
	void stripsSurroundingBlanks() {
		assertEquals("say hi there", HostCommand.normalize("\t say hi there \n"));
	}

	@Test
	void givesNullWhenNothingIsLeft() {
		assertNull(HostCommand.normalize(null));
		assertNull(HostCommand.normalize(""));
		assertNull(HostCommand.normalize("   "));
		assertNull(HostCommand.normalize(" / "));
	}
}

package dev.halfcraft.mobs;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import net.minecraft.world.phys.Vec3;
import org.junit.jupiter.api.Test;

class PetsFollowTest {
	@Test
	void walkingSprintingAndShortTeleportsAreNoJump() {
		Vec3 here = new Vec3(6149.0, 6.0, 29.0);
		assertFalse(PetsFollow.jumped(here, here.add(0.4, 0.0, 0.0)));
		assertFalse(PetsFollow.jumped(here, here.add(8.0, 0.0, 0.0)));  // a chorus fruit
		assertFalse(PetsFollow.jumped(here, here.add(20.0, -8.0, 20.0)));
	}

	@Test
	void anotherMapsSlotIsAJump() {
		Vec3 here = new Vec3(6149.0, 6.0, 29.0);
		assertTrue(PetsFollow.jumped(here, new Vec3(7173.0, 2.0, -60.0)));  // the next slot, 1024 blocks east
		assertTrue(PetsFollow.jumped(here, here.add(0.0, 0.0, 40.0)));
	}
}

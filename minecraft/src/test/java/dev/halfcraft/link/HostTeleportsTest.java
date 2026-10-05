package dev.halfcraft.link;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;

class HostTeleportsTest {
	@AfterEach
	void reset() {
		HostTeleports.reset();
	}

	@Test
	void minecraftsOwnTeleportCountsOnceApplied() {
		HostTeleports.serverSent(5);
		assertEquals(0, HostTeleports.count());
		assertTrue(HostTeleports.clientApplied(5, true));
		assertEquals(1, HostTeleports.count());
	}

	@Test
	void aTeleportCountsOnlyOnce() {
		HostTeleports.serverSent(5);
		HostTeleports.clientApplied(5, true);
		assertFalse(HostTeleports.clientApplied(5, true));
		assertEquals(1, HostTeleports.count());
	}

	@Test
	void halfLifesOwnTeleportDoesNotCount() {
		HostTeleports.byHost(() -> HostTeleports.serverSent(6));
		assertFalse(HostTeleports.clientApplied(6, true));
		assertEquals(0, HostTeleports.count());
	}

	@Test
	void teleportsAfterHalfLifesCountAgain() {
		HostTeleports.byHost(() -> HostTeleports.serverSent(6));
		HostTeleports.serverSent(7);
		assertFalse(HostTeleports.clientApplied(6, true));
		assertTrue(HostTeleports.clientApplied(7, true));
		assertEquals(1, HostTeleports.count());
	}

	@Test
	void unmarkedPacketDoesNotCount() {
		assertFalse(HostTeleports.clientApplied(9, true));
		assertEquals(0, HostTeleports.count());
	}

	@Test
	void teleportThatDidNotMoveThePlayerDoesNotCount() {
		HostTeleports.serverSent(8);
		assertFalse(HostTeleports.clientApplied(8, false));
		assertFalse(HostTeleports.clientApplied(8, true));
		assertEquals(0, HostTeleports.count());
	}

	@Test
	void marksNeverAppliedAreDroppedOldestFirst() {
		for (int id = 0; id <= HostTeleports.MAX_PENDING; id++) {
			HostTeleports.serverSent(id);
		}
		assertFalse(HostTeleports.clientApplied(0, true));
		assertTrue(HostTeleports.clientApplied(HostTeleports.MAX_PENDING, true));
		assertTrue(HostTeleports.clientApplied(1, true));
	}
}

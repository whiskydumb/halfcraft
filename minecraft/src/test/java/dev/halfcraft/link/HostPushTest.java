package dev.halfcraft.link;

import static org.junit.jupiter.api.Assertions.assertArrayEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;

import org.junit.jupiter.api.Test;

class HostPushTest {
	private static final double EPS = 1e-9;

	@Test
	void noPushUntilHalfLifeSendsOne() {
		assertNull(new HostPush().velocity(0));
	}

	@Test
	void pushIsInBlocksPerSecond() {
		HostPush push = new HostPush();
		push.accept(1500, 0, -250, 1000);
		assertArrayEquals(new double[] { 1.5, 0.0, -0.25 }, push.velocity(1100), EPS);
	}

	@Test
	void zeroPushStopsIt() {
		HostPush push = new HostPush();
		push.accept(1500, 0, 0, 1000);
		push.accept(0, 0, 0, 1100);
		assertNull(push.velocity(1100));
	}

	@Test
	void pushNotRepeatedGoesStale() {
		HostPush push = new HostPush();
		push.accept(0, 2000, 0, 1000);
		assertNotNull(push.velocity(1000 + Proto.PUSH_STALE_MS));
		assertNull(push.velocity(1000 + Proto.PUSH_STALE_MS + 1));
		assertNull(push.velocity(1000 + 10 * Proto.PUSH_STALE_MS));
	}

	@Test
	void repeatedPushLasts() {
		HostPush push = new HostPush();
		long now = 1000;
		for (int i = 0; i < 10; i++, now += Proto.PUSH_REPEAT_MS) {
			push.accept(0, 2000, 0, now);
			assertNotNull(push.velocity(now + Proto.PUSH_REPEAT_MS));
		}
	}

	@Test
	void stoppedPushLeavesItsMomentumOnce() {
		HostPush push = new HostPush();
		push.accept(0, 3000, 500, 1000);
		push.accept(0, 0, 0, 1100);
		assertArrayEquals(new double[] { 0.0, 3.0, 0.5 }, push.takeReleased(), EPS);
		assertNull(push.takeReleased());
	}

	@Test
	void stalePushLeavesNoMomentum() {
		HostPush push = new HostPush();
		push.accept(0, 3000, 0, 1000);
		assertNull(push.velocity(1000 + Proto.PUSH_STALE_MS + 1));
		push.accept(0, 0, 0, 5000);
		assertNull(push.takeReleased());
	}

	@Test
	void pushThatStartsAgainLeavesNoMomentum() {
		HostPush push = new HostPush();
		push.accept(1000, 0, 0, 1000);
		push.accept(0, 0, 0, 1100);
		push.accept(2000, 0, 0, 1200);
		assertNull(push.takeReleased());
	}

	@Test
	void noMomentumWithoutAPush() {
		HostPush push = new HostPush();
		push.accept(0, 0, 0, 1000);
		assertNull(push.takeReleased());
	}

	@Test
	void callerCannotChangeThePush() {
		HostPush push = new HostPush();
		push.accept(1000, 0, 0, 0);
		push.velocity(0)[0] = 9.0;
		assertArrayEquals(new double[] { 1.0, 0.0, 0.0 }, push.velocity(0), EPS);
	}
}

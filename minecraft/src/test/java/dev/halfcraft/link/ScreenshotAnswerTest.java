package dev.halfcraft.link;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;

class ScreenshotAnswerTest {
	private static final String FRAME = "C:\\Users\\Гордон Фримен\\AppData\\Local\\Temp\\halfcraft-screenshot-123-1.rgb";

	@Test
	void readsAFrameWhosePathHasSpacesAndCyrillic() {
		ScreenshotAnswer answer = ScreenshotAnswer.parse("ok 7 3440 1440 " + FRAME);
		assertTrue(answer.ok());
		assertEquals(7, answer.request());
		assertEquals(3440, answer.width());
		assertEquals(1440, answer.height());
		assertEquals(FRAME, answer.path());
		assertEquals(3440L * 1440 * 3, answer.bytes());
	}

	@Test
	void readsHalfLifesOwnRequest() {
		assertEquals(ScreenshotAnswer.HOST_REQUEST, ScreenshotAnswer.parse("ok 0 1920 1080 " + FRAME).request());
	}

	@Test
	void readsAFailureWithItsReason() {
		ScreenshotAnswer answer = ScreenshotAnswer.parse("fail 3 Half-Life drew no frame");
		assertFalse(answer.ok());
		assertEquals(3, answer.request());
		assertEquals("Half-Life drew no frame", answer.reason());
	}

	@Test
	void readsAFailureWithoutAReason() {
		assertEquals("", ScreenshotAnswer.parse("fail 3").reason());
	}

	@Test
	void refusesFilesThatArentHalfLifesFrames() {
		assertNull(ScreenshotAnswer.parse("ok 1 640 480 C:\\Users\\gordon\\.minecraft\\options.txt"));
		assertNull(ScreenshotAnswer.parse("ok 1 640 480 C:\\Temp\\halfcraft-screenshot-1.png"));
	}

	@Test
	void refusesBrokenAnswers() {
		assertNull(ScreenshotAnswer.parse(""));
		assertNull(ScreenshotAnswer.parse("ok 1 640 480"));
		assertNull(ScreenshotAnswer.parse("ok x 640 480 " + FRAME));
		assertNull(ScreenshotAnswer.parse("ok 1 0 480 " + FRAME));
		assertNull(ScreenshotAnswer.parse("ok 1 640 99999 " + FRAME));
		assertNull(ScreenshotAnswer.parse("fail "));
		assertNull(ScreenshotAnswer.parse("maybe 1 2 3"));
	}

	@Test
	void turnsRgbIntoOpaqueAbgr() {
		byte[] rgb = { 0, 0, 0, (byte) 0x12, (byte) 0x80, (byte) 0xFF };
		assertEquals(0xFF000000, ScreenshotAnswer.abgr(rgb, 0));
		assertEquals(0xFFFF8012, ScreenshotAnswer.abgr(rgb, 3));
	}
}

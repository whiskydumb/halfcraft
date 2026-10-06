package dev.halfcraft.combat;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import dev.halfcraft.link.Proto;
import org.junit.jupiter.api.Test;

class HostHurtsTest {
	@Test
	void shouldTurnBlastsIntoPositionedExplosionsBlamedOnTheThrower() {
		HostHurts.Recipe thrown = HostHurts.recipe(Proto.HURT_BLAST, true);
		assertEquals(HostHurts.BLAST, thrown.typeId());
		assertTrue(thrown.positioned());
		assertTrue(thrown.standInCausing());
		assertFalse(thrown.standInDirect());

		HostHurts.Recipe barrel = HostHurts.recipe(Proto.HURT_BLAST, false);
		assertEquals(HostHurts.BLAST, barrel.typeId());
		assertTrue(barrel.positioned());
		assertFalse(barrel.standInCausing());
	}

	@Test
	void shouldTurnBurnsIntoFireAndCrushesIntoArmouredDamage() {
		assertEquals(HostHurts.BURN, HostHurts.recipe(Proto.HURT_FIRE, false).typeId());
		assertEquals(HostHurts.CRUSH, HostHurts.recipe(Proto.HURT_CRUSH, false).typeId());
		assertTrue(HostHurts.recipe(Proto.HURT_CRUSH, false).positioned());
	}

	@Test
	void shouldKeepMeleeAndBulletsOnTheStandInWhenThereIsOne() {
		HostHurts.Recipe melee = HostHurts.recipe(Proto.HURT_MELEE, true);
		assertEquals("minecraft:mob_attack", melee.typeId());
		assertTrue(melee.standInDirect());
		assertFalse(melee.positioned());
		assertEquals("minecraft:mob_projectile", HostHurts.recipe(Proto.HURT_PROJECTILE, true).typeId());
	}

	@Test
	void shouldGiveFarMeleeAndBulletsTheirOwnPositionedTypesInsteadOfGeneric() {
		HostHurts.Recipe bullet = HostHurts.recipe(Proto.HURT_PROJECTILE, false);
		assertEquals(HostHurts.BULLET, bullet.typeId());
		assertTrue(bullet.positioned());
		assertFalse(bullet.standInDirect());
		assertEquals(HostHurts.MELEE, HostHurts.recipe(Proto.HURT_MELEE, false).typeId());
	}

	@Test
	void shouldLeaveMagicAndUnknownKindsAsBefore() {
		assertEquals("minecraft:indirect_magic", HostHurts.recipe(Proto.HURT_MAGIC, true).typeId());
		assertEquals("minecraft:magic", HostHurts.recipe(Proto.HURT_MAGIC, false).typeId());
		assertEquals("minecraft:generic", HostHurts.recipe(Proto.HURT_OTHER, true).typeId());
		assertEquals("minecraft:generic", HostHurts.recipe(15, false).typeId());
	}

	@Test
	void shouldTellHalfCraftsOwnTypesWhichKnockBackOnlyFromAPosition() {
		assertTrue(HostHurts.isOwnType(HostHurts.BLAST));
		assertTrue(HostHurts.isOwnType(HostHurts.MELEE));
		assertTrue(HostHurts.isOwnType(HostHurts.BULLET));
		assertTrue(HostHurts.isOwnType(HostHurts.CRUSH));
		assertFalse(HostHurts.isOwnType("minecraft:in_fire"));
		assertFalse(HostHurts.isOwnType("minecraft:mob_attack"));
	}

	@Test
	void shouldKnockBackFromHalfCraftsOwnHitsButNotFromBurns() {
		assertTrue(HostHurts.knocksBack(HostHurts.BLAST));
		assertTrue(HostHurts.knocksBack(HostHurts.CRUSH));
		assertFalse(HostHurts.knocksBack(HostHurts.BURN));
		assertFalse(HostHurts.knocksBack("minecraft:in_fire"));
	}

	@Test
	void shouldDecodeTheHurtPositionFromFloatBits() {
		assertEquals(1024.5, HostHurts.coordinate(Float.floatToRawIntBits(1024.5F)), 0.0);
		assertEquals(-3.25, HostHurts.coordinate(Float.floatToRawIntBits(-3.25F)), 0.0);
	}
}

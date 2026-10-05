package dev.halfcraft.mobs;

/** A pathfinding search remembers whether its mob is in the mirror world (PathfindingContextMixin). */
public interface HostNavContext {
	boolean halfcraft$inMirror();
}

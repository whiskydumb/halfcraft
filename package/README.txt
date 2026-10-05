HalfCraft {VERSION}
==================

Half-Life 2, played with Minecraft. Real Minecraft runs hidden and does your physics, inventory,
blocks and combat; Half-Life 2 runs its world, NPCs and story around you. Build on its maps, fight
the Combine with a diamond sword, light City 17 with torches. Episode One and Episode Two are in
too: their chapters follow Half-Life 2's in New Game.


YOU NEED

- Windows 10 or 11, 64-bit
- Steam, with Half-Life 2 installed, and its two episodes for their chapters (they come with
  Half-Life 2; HalfCraft.exe offers to install them if Steam hasn't)
- optional: Half-Life 2: Deathmatch. HalfCraft can run on its engine too, which is 64-bit and can
  use more memory than Half-Life 2's own 32-bit one
- a Microsoft account that owns Minecraft: Java Edition
- about 1 GB of free space for Minecraft and Java, which download on the first start


PLAY

1. Unpack the zip anywhere, e.g. C:\Games\HalfCraft. A path with only English letters is safest.
2. Start HalfCraft.exe. If you have Half-Life 2: Deathmatch too, it asks which engine to run and
   can remember your choice. Hold Shift while starting HalfCraft.exe to choose again.
3. The first time, Prism Launcher opens: sign in with your Microsoft account. Half-Life 2 starts
   once you're signed in. Minecraft downloads (a few minutes the first time) and joins by itself;
   a line at the top of the screen says how far along it is.
4. Start a new game, or load a save.

Arguments for Half-Life 2 go after HalfCraft.exe, e.g. a shortcut to
   HalfCraft.exe -windowed -w 1920 -h 1080


CONTROLS

Minecraft's, as you set them in Minecraft (WASD, mouse, E for the inventory, 1-9, F5 for the
camera, ...), plus Half-Life's own:

  G          use: doors, buttons, chargers, ladders, picking up props
             (or whatever key Half-Life's keyboard options bind to Use)
  mouse      while carrying a prop: left throws it, right drops it
  V          flashlight
  Esc        Half-Life's menu: save, load, options, quit
  F6 / F9    quick save / quick load


SAVES

Half-Life's saves are in game-hl2\save, or game-hl2dm\save on Deathmatch's engine: each engine
keeps its own. Minecraft's world is in
minecraft\Prism\instances\HalfCraft\.minecraft\saves\HalfCraft.
Loading a Half-Life save rolls Minecraft's blocks and your inventory back to that save too.


UPDATING

Unpack the new zip over this folder and overwrite. Your saves, your Minecraft account and the
downloaded Minecraft stay.


IF SOMETHING'S WRONG

- The line at the top of the screen says what Minecraft is doing. If it says Minecraft isn't
  running, quit and start HalfCraft.exe again.
- Logs: game-hl2\console.log or game-hl2dm\console.log (Half-Life, by engine),
  minecraft\Prism\instances\HalfCraft\.minecraft\logs\latest.log (Minecraft), minecraft\Prism\logs
  (Prism Launcher).


HalfCraft is MIT-licensed (LICENSE.txt) and based on SkyCraft by chasmlol. What it bundles is
listed in THIRD-PARTY-NOTICES.md. It isn't affiliated with or endorsed by Valve, Mojang or Microsoft.

# Third-party notices

HalfCraft is MIT-licensed (see `LICENSE`). It is built from, or a release contains, the following.

## The Source mod (`client.dll`, `server.dll`)

| Component | License | Source |
|---|---|---|
| Source SDK 2013 for Half-Life 2: Deathmatch's engine, via the hl2dm-sp fork (pinned in `tools/halfcraft/sdk.py`) | Source 1 SDK License (Valve) | https://github.com/hardlightbridge/hl2dm-sp, https://github.com/ValveSoftware/source-sdk-2013 |
| Source SDK 2013 for Half-Life 2's engine: Valve's singleplayer tree as of 2015 (pinned in `tools/halfcraft/sdk.py`) | Source 1 SDK License (Valve) | https://github.com/ValveSoftware/source-sdk-2013 |

`source/sdk/halfcraft-hl2.patch` and `source/sdk/halfcraft-hl2dm.patch` are modifications of SDK
files and fall under the same license. The SDK itself isn't in this repository: `tools/build/setup_sdk.py`
downloads it.

## The bundled Minecraft (when packaged)

| Component | License | Source |
|---|---|---|
| Prism Launcher (unmodified portable Windows build) | GPL-3.0 | https://github.com/PrismLauncher/PrismLauncher |
| Fabric API | Apache-2.0 | https://github.com/FabricMC/fabric |

## Not included

Half-Life 2, Half-Life 2: Deathmatch (whose engine can run the mod instead of Half-Life 2's), Minecraft,
Java and Fabric Loader aren't included. Half-Life 2 and Half-Life 2: Deathmatch come from Steam; Prism Launcher
downloads Minecraft, Java and Fabric Loader after the player signs in with a Microsoft account
that owns Minecraft: Java Edition.

HalfCraft isn't affiliated with or endorsed by Valve, Mojang or Microsoft.

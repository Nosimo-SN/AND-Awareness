# A.N.D. Awareness

A [SkyrimNet](https://fateless.ai) add-on that makes characters aware of what
[Advanced Nudity Detection (A.N.D.)](https://www.nexusmods.com/skyrimspecialedition/mods/165289) knows about clothing:
who is showing what, which garments are sheer or cover nothing, and when a curtain or sheer garment
"flashes" for a moment.

It has two parts that work together:

| Part | What it does |
|---|---|
| **Clothing Coverage prompt** (`0413_clothingcoverage.prompt`) | Adds a *Clothing Coverage* section to every character bio, under the equipment list: revealing garments, what is visible right now, which garment is responsible, and an overall clothing-quality note. |
| **AND_Awareness SKSE plugin** (`AND_Awareness.dll`) | Watches A.N.D.'s flash states on the player and nearby NPCs and posts a short narration line when a flash starts, so nearby characters can react. Fully adjustable from SkyrimNet's settings page. No Papyrus scripts. |

> **Adult content.** This mod describes nudity and exposure states reported by A.N.D. It is intended for
> adult players only.

## Requirements

- Skyrim SE / AE (1.5.97 and 1.6.x) with [SKSE](https://skse.silverlock.org/)
- [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444)
- Advanced Nudity Detection 4.x
- SkyrimNet Beta 25 or later (for the plugin settings page)
- **Keyword Item Distributor (KID) files that tag your clothing with A.N.D. keywords.** The mod only knows
  what A.N.D. knows: the better your clothing is tagged, the richer the descriptions.

## Installation

Install with your mod manager. The archive layout is:

```
SKSE/Plugins/AND_Awareness.dll
SKSE/Plugins/AND_Awareness.pdb                      (optional, makes crash logs readable)
SKSE/Plugins/SkyrimNet/external/nosimo.and_awareness/
    manifest.json
    prompts/submodules/character_bio/0413_clothingcoverage.prompt
    settings/AND_Awareness.yaml
```

## How it works

### Clothing Coverage prompt

- **Exposure comes only from A.N.D.** Every "visible" or "bare" line is A.N.D.'s own verdict (its exposure
  factions). Keywords never invent exposure.
- **Keywords add detail:** the prompt reads A.N.D. keywords on worn items to name the garment involved and
  to list sheer (`T`, `T_Low`, `T_High`) and cover-nothing (`_NoCover`) garments. Male actors use the
  `_Male` keyword set, as A.N.D. does.
- **Flashes are described as coverage, not movement.** A.N.D.'s flash is a logical "not covering right now"
  roll, so the prompt says "the dress isn't covering this" rather than inventing motion.
- **Conflicting tags:** when one garment carries several A.N.D. keywords for the same layer (common when
  several KID files overlap), the least revealing one wins, matching A.N.D.'s "modesty overrules skimpy"
  behaviour (verified in game).
- **Clothing quality:** vanilla `ClothingRich` / `ClothingPoor` keywords give an overall quality note.

### AND_Awareness SKSE plugin

- Every 2 seconds it reads A.N.D.'s ten flashing factions on the player and on NPCs loaded near the player.
- When a flash **starts** and passes your settings, it posts one narration line to SkyrimNet with that
  character as the originator, so a nearby NPC may react. Flash endings are left to A.N.D., which plays
  its own covering animations.
- No narration on loading a save: flashes already in progress are only recorded.
- Every flash start and end also raises the SKSE mod event **`SN_ANDFlash`** for other mods:
  `strArg` = flash type (`ChestCurtain`, `PelvicCurtain`, `AssCurtain`, `Top`, `Bra`, `Bottom`,
  `Underwear`, `Hotpants`, `Skirt`, `CString`), `numArg` = `1` start / `0` end, sender = the actor.
- All SkyrimNet calls happen off the game thread, so the game never waits on SkyrimNet.

## Settings

In SkyrimNet: **Settings > Plugins > AND_Awareness**. Changes apply within about 10 seconds.

| Setting | Default | |
|---|---|---|
| Enable Flash Reactions | on | Master switch (the `SN_ANDFlash` mod event still fires) |
| React To Player / NPC Flashes | on / on | Which characters can trigger a reaction |
| NPCs: Followers Only | off | Only your followers' flashes |
| NPCs: Max Distance | 1500 | Game units (about 70 per metre); 0 = no limit |
| Reaction Chance (%) | 25 | Lower it if most of your clothing is tagged |
| Global Cooldown (seconds) | 180 | Quiet time after any reaction |
| Per-Actor Cooldown (seconds) | 600 | Quiet time per character |
| Force A Reaction | on | On = an NPC reacts right away; off = a quiet scene event NPCs may bring up later |
| Flash Types | all on | One toggle per flash type |
| Narration Text | | One editable line per flash type; placeholders `{name}`, `{their}`, `{chest}`, `{band}` |
| Log Every Flash | off | Writes every flash to `AND_Awareness.log` in the SKSE log folder |

## Building from source

Needs Visual Studio 2022 Build Tools (MSVC, C++23), CMake 3.21+, Ninja (included with the Build Tools),
Git and [vcpkg](https://github.com/microsoft/vcpkg).

```powershell
git clone --recurse-submodules https://github.com/Nosimo-SN/AND-Awareness.git
cd AND-Awareness
```

**SkyrimNet API headers:** copy `PublicAPI.h`, `PublicAPIMemoryQuery.h` and `PublicAPIDiaryQuery.h` from
the SkyrimNet developer kit's `CppAPI/` folder into `include/SkyrimNet/`. They are SkyrimNet's files and
are not redistributed here.

Then, from a VS Developer PowerShell (`Launch-VsDevShell.ps1 -Arch amd64 -HostArch amd64`):

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
cmake --build build --target AND_Awareness
```

The first build compiles CommonLibSSE-NG (about 15 minutes); later builds take seconds. Built against
[CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng) (alandtse `ng` branch, 13.1.0).

| Path | What |
|---|---|
| `src/plugin.cpp` | the whole SKSE plugin |
| `src/PCH.h` | precompiled header |
| `skyrimnet/nosimo.and_awareness/` | the SkyrimNet plugin: manifest, prompt, settings page |
| `lib/commonlibsse-ng/` | CommonLibSSE-NG (git submodule) |
| `include/SkyrimNet/` | SkyrimNet API headers (not included, see above) |

## Known limits

- **Only as good as A.N.D.'s state.** A.N.D. updates on equip changes and on a timer (every 15 in-game
  minutes by default), not on save load, so a character can briefly show an old state, e.g. right after
  a mod re-dresses them.
- **NPC detection covers characters loaded near the player** (the engine's high-process actor list).
- Tested on Skyrim 1.6.1170. Skyrim 1.7.x should work through Address Library but is not yet verified.

## Credits

- **Advanced Nudity Detection** and its keyword/faction design, by its author.
- **SkyrimNet** and its developer kit, by the SkyrimNet team.
- **CommonLibSSE-NG** (alandtse `ng`).

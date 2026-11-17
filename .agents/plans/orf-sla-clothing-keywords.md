# ORF and SLA keywords in the Clothing Coverage prompt

## Goal

`0413_clothingcoverage.prompt` also reads Outfit Recognition Framework (`ORF_*`) and SexLab Aroused (`SLA_*`)
keywords on worn items and writes one descriptive line per tagged garment. Both mods stay optional: the
prompt matches EditorIDs only, so missing keywords render nothing. Prompt-only change, no DLL change.

## Rules (unchanged)

- Exposure ("visible", "bare") comes only from A.N.D. factions. ORF/SLA never create exposure lines.
- Children skipped (existing `isChild` gate).
- Section renders only if something is worth saying; a garment line now counts as such.

## Keywords in the load order

ORF (`Outfit Recognition Framework.esp`, 34): colours `Red Blue Green Yellow Black Grey Brown White Pink Purple
Silver Gold Colourful`, material/fit `Leather Tight Flimsy Sturdy Transparent Hooded`, shape `Dress Robes
ShortSkirt Bra Underwear`, reveal `Cleavage Revealing NearlyNaked`, impression `Classy Scruffy Intimidating
Inappropriate OutOfPlace FromOtherGame`, `Ignore`.

SLA (`SexLabAroused.esm`, 49): clothing `ArmorPretty ArmorHalfNaked ArmorHalfNakedBikini ArmorSpendex
ArmorTransparent ArmorRubber ArmorHarness ArmorLewdLeotard ArmorCurtain ArmorCapeFull ArmorCapeMini
ArmorBondage ArmorIllegal ImpossibleClothes ArmorPartTop ArmorPartBottom ArmorFemaleOnly`, lower
`PantiesNormal PantsNormal MicroHotpants ThongT ThongLowleg ThongCString ThongGstring FullSkirt MiniSkirt
MicroSkirt ShowgirlSkirt PelvicCurtain`, upper `Brabikini PastiesNipple PastiesCrotch`, legs/feet
`HasLeggings HasStockings HasSleeves BootsHeels KillerHeels`, jewellery `Piercing* Earrings`, toys
`AnalPlug* VaginalBeads VaginalDildo`.

## Output

One line per worn garment that has at least one descriptor, under a `- Garments:` line:

```
- Garments:
  - Thieves Guild Armor Remodeled: brown leather armor, low-cut, with a miniskirt.
  - Thieves Guild Boots: high-heeled boots.
  - Blackguard's Boots: boots on towering stiletto heels.
```

Phrase order: `[colours] [material] [fit] <noun>[, cut][, with <extras>].` No articles (avoids a/an and
plural handling). Colours joined with "and" ("black and gold"). Colours are always included when tagged.

| Part | Keywords → words |
|---|---|
| Colours | ORF colours → "red", ..., ORF_Colourful → "colourful" |
| Material | ORF_Leather → "leather", SLA_ArmorRubber → "rubber" |
| Fit | ORF_Tight / SLA_ArmorSpendex → "skin-tight" (once), ORF_Flimsy → "flimsy", ORF_Sturdy → "sturdy" |
| Armor | vanilla ArmorLight or ArmorHeavy on the item = armored; otherwise clothing |
| Noun (first hit) | Shape: SLA_ArmorLewdLeotard → "leotard", SLA_ArmorHarness → "harness", SLA_ArmorHalfNakedBikini / SLA_Brabikini → "bikini", ORF_Dress → "dress", ORF_Robes → "robes"; prefixed "armored" when armored ("armored dress"). Else by slot (ArmorCuirass/ClothingBody, ArmorBoots/ClothingFeet, ArmorGauntlets/ClothingHands, ArmorHelmet/ClothingHead), armored / clothing: body "armor" / "clothes", feet "armored boots" / "boots", hands "gauntlets" / "gloves", head "helmet" / "hood or hat" → "headwear"; else "armor piece" / "garment" |
| Heels (feet only) | SLA_BootsHeels → noun prefixed "high-heeled"; SLA_KillerHeels → "<noun> on towering stiletto heels" (wins over BootsHeels) |
| Cut | ORF_Cleavage → "low-cut", ORF_Revealing → "revealing", SLA_ArmorHalfNaked → "leaving much of the body bare", ORF_NearlyNaked → "leaving the wearer nearly naked" (wins over HalfNaked) |
| Extras ("with ...") | ORF_ShortSkirt / SLA_MiniSkirt → "a miniskirt", SLA_MicroSkirt → "a microskirt" (wins), SLA_FullSkirt → "a full skirt", SLA_MicroHotpants → "tiny shorts", ORF_Hooded → "a hood", SLA_ArmorCapeFull / SLA_ArmorCapeMini → "a cape" / "a short cape", SLA_HasSleeves → "sleeves", SLA_HasStockings → "stockings", SLA_HasLeggings → "leggings", SLA_Thong* → "a thong", SLA_PastiesNipple → "nipple pasties" |

Only colours, material, fit, heels, cut or extras make a line; a bare slot noun alone renders nothing.

Sheerness: no word in the garment line. It stays in the existing "Garment transparency and coverage" line:
an item with A.N.D. layer keywords keeps A.N.D.'s rank and ignores ORF/SLA transparent ("modesty overrules
skimpy"); an item without A.N.D. layer keywords but with ORF_Transparent or SLA_ArmorTransparent is listed
there as "sheer" (rank 2).

Quality: ORF_Classy counts as rich and ORF_Scruffy as poor in the existing ClothingRich/ClothingPoor tally.

Impression (ORF_Intimidating, ORF_Inappropriate, ORF_OutOfPlace, SLA_ArmorPretty, SLA_ArmorIllegal,
SLA_ImpossibleClothes): appended to the existing Attire line, e.g.
`- Attire: contains garments classified as fine clothing; looks intimidating.` The Attire line renders when
either quality or impression exists.

ORF_Ignore drops the item's ORF words.

Out of scope: piercings, earrings, toys (later addition).

Ignored: ORF_FromOtherGame, ORF_Bra, ORF_Underwear, SLA_PantiesNormal, SLA_PantsNormal, SLA_ArmorPart*,
SLA_ArmorFemaleOnly, SLA_PelvicCurtain, SLA_ShowgirlSkirt, SLA_ArmorCurtain, SLA_ArmorBondage,
SLA_PastiesCrotch (A.N.D. layers cover them, or they add nothing an LLM can use).

## Steps

1. Branch `feature/orf-sla-keywords`.
2. Prompt: in the item loop add pass 3 that fills per-item part variables (colours list, material, fit list,
   noun, heels, cut, extras list) and outfit impression flags; build the phrase and append to parallel
   `cc_gar_names` / `cc_gar_text` lists. Match case-insensitively (`lower(kw.editorID)`): KID files spell
   `sla_` / `SLA_`. Render `- Garments:` after the transparency line; extend the Attire line.
3. Header comment: document sources and that ORF/SLA never infer exposure.
4. README: "How it works" bullet, optional ORF/SLA in Requirements; manifest description.
5. Version 1.1.0 in `manifest.json`, `AND_Awareness.yaml` (and `plugin.cpp`/CMake if versioned there).
6. In-game check (no tests for prompts): an ORF-tagged vanilla outfit, an SLA-tagged outfit with heels, an
   untagged outfit (no new lines), a child (nothing), payload read from SkyrimNet logs.

## Known overlap (accepted)

SkyrimNet_SexLab's `0005_sexlab_variables.prompt` / `0415_sexlab_appearance.prompt` write one generic
"revealing clothing" sentence for `SLA_ArmorSpendex`, `SLA_ArmorHalfNaked`, `SLA_ArmorHalfNakedBikini`.
This prompt still describes those items.

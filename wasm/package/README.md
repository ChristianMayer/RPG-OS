# @mundus-mirabilis/rpg-os

The **universal RPG-OS rule engine**, compiled to WebAssembly. It is *universal*
because it loads **any** ruleset JSON at runtime — the WASM binary never embeds
a game's data. All rule evaluation (character stats, derived stats, checks,
skill rolls, combat simulation, hit points) runs in the compiled C++ engine.

Works in **Node.js** and in the **browser** (no server needed).

## Install

```sh
npm install @mundus-mirabilis/rpg-os
```

## Quick start — Node.js

```js
import { init } from '@mundus-mirabilis/rpg-os';
import { readFileSync } from 'node:fs';

const rpg = await init();
// Load the JSON with Node's native fs — nothing is baked into the WASM.
rpg.loadRulesetFromFile('node_modules/@mundus-mirabilis/rpg-os/rulesets/tde5e_core.json');

const geron = rpg.createEntity('geron');
console.log(geron.getStat('COU'));        // 12
console.log(geron.getResource('LP'));     // 31  (5 + 2*CON)
```

## Quick start — browser

```js
import { init } from '@mundus-mirabilis/rpg-os';

const rpg = await init();
const ruleset = await (await fetch('rulesets/dnd5e_srd.json')).text();
rpg.loadRuleset(ruleset);

const hero = rpg.createEntityFromSheet('my_hero', {
  STR: 16, DEX: 14, CON: 15, INT: 10, WIS: 12, CHA: 8,
  Attack: 5, AC: 17, Initiative: 2, HitPoints_Max: 30,
});
```

## API

| Method | Description |
| --- | --- |
| `await init()` | Loads the WASM module and returns an `RpgOs` instance. |
| `rpg.loadRuleset(json)` | Loads a ruleset from a JSON string (both runtimes). |
| `rpg.loadRulesetFromFile(path)` | Node-only: reads the JSON with `fs`. |
| `rpg.meta()` | Ruleset metadata (attributes, skills, pools) for building forms. |
| `rpg.entries()` | Named archetypes + bestiary entries in the ruleset. |
| `rpg.createEntity(id, opts?)` | Creates an entity from a named entry. |
| `rpg.createEntityFromSheet(id, stats)` | Creates an entity from **arbitrary** stats — any character, even one with no ruleset entry. |
| `entity.getStat/getResource/setStat/...` | Read/write stats and resource pools (LP/HP/AE/…). |
| `rpg.check(checkType, actor, target?, opts?)` | Resolves a named check. |
| `rpg.specFromId(id, weapon?)` / `rpg.specFromEntity(entity, weapon?)` | Build a combatant. |
| `rpg.fight(specA, specB, opts?)` | Runs one fight; loop it for Monte-Carlo win probabilities. |
| `rpg.dispose()` | Releases the engine and all handles. |

## Rulesets

The package ships the project's rulesets under `rulesets/` as plain JSON data
(`dnd5e_srd.json`, `tde5e_core.json`, `brp_ugc.json`) for convenience. Because
they are loaded at runtime, you can pass **any** ruleset that follows the
[RPG-OS ruleset schema](https://github.com/Mundus-Mirabilis/RPG-OS/blob/main/rulesets/ruleset.schema.json).

## License

Apache-2.0 — see [LICENSE](./LICENSE).

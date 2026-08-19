// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @module @mundus-mirabilis/rpg-os
 *
 * Universal RPG-OS rule engine compiled to WebAssembly. The package loads any
 * ruleset JSON at runtime — Node reads it with `fs`, browsers fetch it and
 * pass the string — so the engine never embeds a game's data: it is truly
 * universal. All rule evaluation (stats, checks, combat, sheets) runs in the
 * WASM binary; this module is a thin ergonomic wrapper around the flat C API.
 *
 * ```js
 * import { init } from '@mundus-mirabilis/rpg-os';
 * const rpg = await init();
 * rpg.loadRuleset(await (await fetch('rulesets/tde5e_core.json')).text());
 * const geron = rpg.createEntity('geron');
 * console.log(geron.getStat('COU'), geron.getResource('LP'));
 * ```
 *
 * @par Node vs browser
 * `init()` works in both (the WASM is loaded from `dist/` next to this
 * module). In Node you can also use `loadRulesetFromFile(path)`, which reads
 * the file with the built-in `fs` module — the "load JSON natively" path.
 *
 * @par Shared wrapper
 * The Entity / CombatantSpec / RpgOs classes and the marshaling live in
 * `core.mjs` (copied from web/demo/rpg-core.js by wasm/build.sh), so the npm
 * package and the browser demo are guaranteed to behave identically. This
 * module adds only the Node-specific `loadRulesetFromFile` on top.
 */

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import createRpgOsModule from './dist/rpg-os-universal.js';
import { Entity, CombatantSpec, RpgOs } from './core.mjs';

const isNode = typeof process !== 'undefined' && !!process.versions?.node;

let modulePromise = null;

/** The emscripten module singleton, instantiated once per process. */
function getModule() {
  if (modulePromise === null) {
    modulePromise = createRpgOsModule({
      locateFile(file) {
        const url = new URL(`./dist/${file}`, import.meta.url);
        return isNode ? fileURLToPath(url) : url.href;
      },
    });
  }
  return modulePromise;
}

/** The Node engine: the shared RpgOs plus `loadRulesetFromFile`. */
class RpgOsNode extends RpgOs {
  /**
   * Loads a ruleset from a file path using Node's built-in `fs` module —
   * the "load the JSON with native functionality" path. Browser only: fetch
   * the file and pass the text to {@link loadRuleset}.
   */
  loadRulesetFromFile(filePath) {
    if (!isNode) {
      throw new Error(
        'loadRulesetFromFile is only available in Node.js — fetch the file and pass its text to loadRuleset()');
    }
    return this.loadRuleset(readFileSync(filePath, 'utf8'));
  }
}

/**
 * Initializes the engine (loads and instantiates the WASM module once).
 * Works in Node and the browser.
 */
export async function init() {
  const m = await getModule();
  const enginePtr = m._rpg_os_engine_new();
  if (!enginePtr) throw new Error('failed to allocate the RPG-OS engine');
  return new RpgOsNode(m, enginePtr);
}

export { Entity, CombatantSpec, RpgOs };
export default init;

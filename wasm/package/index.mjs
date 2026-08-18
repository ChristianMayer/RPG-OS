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
 */

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import createRpgOsModule from './dist/rpg-os-universal.js';

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

// ---- low-level marshaling over the flat C API ------------------------------

/** Writes `str` into WASM memory; returns `{ ptr, len }` (caller frees ptr). */
function alloc(m, str) {
  const len = m.lengthBytesUTF8(str);
  const ptr = m._malloc(len + 1);
  m.stringToUTF8(str, ptr, len + 1);
  return { ptr, len };
}

/** Frees a pointer returned by @see alloc. */
function free(m, ptr) {
  if (ptr) m._free(ptr);
}

/** Reads a NUL-terminated string from WASM memory. */
function read(m, ptr) {
  return ptr ? m.UTF8ToString(ptr) : '';
}

/** Calls an exported function, parses its JSON result, and unwraps errors. */
function jsonResult(m, fn, ...args) {
  const raw = fn(...args);
  const parsed = raw ? JSON.parse(read(m, raw)) : null;
  if (parsed && parsed.error) throw new Error(parsed.error);
  return parsed;
}

// ---- public API -------------------------------------------------------------

/**
 * A live character sheet (wraps a C++ `DynamicEntity` handle). Stat values are
 * attributes, skill ratings, or derived stats; resource pools (LP/HP/AE/…)
 * are read and modified separately.
 */
export class Entity {
  constructor(m, ptr, onDispose) {
    this._m = m;
    this._ptr = ptr;
    this._onDispose = onDispose;
  }

  /** Sets a base attribute or skill rating. Returns this for chaining. */
  setStat(stat, value) {
    const { ptr } = alloc(this._m, stat);
    try {
      this._m._rpg_os_entity_set_stat(this._ptr, ptr, value);
    } finally {
      free(this._m, ptr);
    }
    return this;
  }

  /** Reads a stat (attribute, skill rating, or derived stat); 0 when unknown. */
  getStat(stat) {
    const { ptr } = alloc(this._m, stat);
    try {
      return this._m._rpg_os_entity_get_stat(this._ptr, ptr);
    } finally {
      free(this._m, ptr);
    }
  }

  /** Current value of a resource pool (e.g. "LP", "HP", "AE"); 0 when absent. */
  getResource(pool) {
    const { ptr } = alloc(this._m, pool);
    try {
      return this._m._rpg_os_entity_get_resource(this._ptr, ptr);
    } finally {
      free(this._m, ptr);
    }
  }

  /** Applies a delta to a resource pool (clamped); returns the amount applied. */
  modifyResource(pool, delta) {
    const { ptr } = alloc(this._m, pool);
    try {
      return this._m._rpg_os_entity_modify_resource(this._ptr, ptr, delta);
    } finally {
      free(this._m, ptr);
    }
  }

  /** (Re)creates the resource pools from the current stats. Returns this. */
  refreshResources() {
    this._m._rpg_os_entity_refresh_resources(this._ptr);
    return this;
  }

  /** Serializes the sheet (the `DynamicEntity` save form) as a plain object. */
  toJson() {
    return JSON.parse(read(this._m, this._m._rpg_os_entity_to_json(this._ptr)));
  }

  /** Releases the underlying WASM handle. */
  dispose() {
    if (this._ptr) {
      this._m._rpg_os_entity_free(this._ptr);
      this._ptr = null;
      this._onDispose?.();
    }
  }
}

/**
 * A static combatant description (wraps a C++ `CombatantSpec` handle) built
 * from a named ruleset entry or from a live `Entity`. Pass two of these to
 * `RpgOs.fight`.
 */
export class CombatantSpec {
  constructor(m, ptr, onDispose) {
    this._m = m;
    this._ptr = ptr;
    this._onDispose = onDispose;
  }

  /** Releases the underlying WASM handle. */
  dispose() {
    if (this._ptr) {
      this._m._rpg_os_spec_free(this._ptr);
      this._ptr = null;
      this._onDispose?.();
    }
  }
}

/** The engine: one loaded ruleset and the operations over it. */
export class RpgOs {
  constructor(m, enginePtr) {
    this._m = m;
    this._ptr = enginePtr;
    this._entities = new Set();
    this._specs = new Set();
  }

  // ---- ruleset -------------------------------------------------------------

  /** Loads a ruleset from a JSON string. Returns this for chaining. */
  loadRuleset(json) {
    const { ptr, len } = alloc(this._m, json);
    try {
      const ok = this._m._rpg_os_engine_load_json(this._ptr, ptr, len);
      if (!ok) {
        const detail = read(this._m, this._m._rpg_os_engine_last_error(this._ptr));
        throw new Error(detail || 'failed to load ruleset');
      }
      return this;
    } finally {
      free(this._m, ptr);
    }
  }

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

  /** The loaded ruleset's metadata (attributes/skills/pools for a form). */
  meta() {
    return jsonResult(this._m, this._m._rpg_os_engine_meta, this._ptr);
  }

  /** The ruleset's named combatants (archetypes + bestiary entries). */
  entries() {
    return jsonResult(this._m, this._m._rpg_os_engine_entries, this._ptr);
  }

  // ---- entities ------------------------------------------------------------

  /**
   * Creates an entity from a named archetype or bestiary entry.
   * `options.variance`: 0 weakest … 4 strongest (2 = average); `options.seed`
   * makes ranged values reproducible.
   */
  createEntity(id, options = {}) {
    const { ptr } = alloc(this._m, id);
    try {
      const h = this._m._rpg_os_entity_create(
        this._ptr, ptr, options.variance ?? 2, (options.seed ?? 1) >>> 0);
      if (!h) throw new Error(`unknown entity id: "${id}"`);
      return this._trackEntity(h);
    } finally {
      free(this._m, ptr);
    }
  }

  /**
   * Creates an entity from an arbitrary character sheet — a plain stats map
   * like `{ COU: 12, Attack: 12, ... }` or a full `DynamicEntity` save form
   * (`{ stats: {...}, resources: {...}, ... }`). This is how a character
   * entered into a form, with no ruleset entry, becomes a first-class entity.
   */
  createEntityFromSheet(id, stats) {
    const sheet = stats && typeof stats === 'object' && 'stats' in stats
      ? JSON.stringify(stats)
      : JSON.stringify({ id, stats: stats ?? {} });
    const idPtr = alloc(this._m, id);
    const { ptr, len } = alloc(this._m, sheet);
    try {
      const h = this._m._rpg_os_entity_create_sheet(this._ptr, idPtr.ptr, ptr, len);
      if (!h) throw new Error(`failed to create entity from sheet: "${id}"`);
      return this._trackEntity(h);
    } finally {
      free(this._m, idPtr.ptr);
      free(this._m, ptr);
    }
  }

  _trackEntity(h) {
    const entity = new Entity(this._m, h, () => this._entities.delete(h));
    this._entities.add(h);
    return entity;
  }

  // ---- checks --------------------------------------------------------------

  /**
   * Resolves a named check for `actor` against `target` (null = solo check).
   * `options.advantage`: +1 advantage, -1 disadvantage, 0 (default) neutral.
   * Returns `{ is_success, is_critical_success, is_critical_failure, margin,
   * remaining_pool, quality_level, raw_dice }`.
   */
  check(checkType, actor, target = null, options = {}) {
    const ct = alloc(this._m, checkType);
    try {
      return jsonResult(
        this._m, this._m._rpg_os_check, this._ptr, ct.ptr,
        actor?._ptr ?? 0, target?._ptr ?? 0, options.advantage ?? 0);
    } finally {
      free(this._m, ct.ptr);
    }
  }

  // ---- combat --------------------------------------------------------------

  /** Builds a combatant from a named archetype or bestiary entry. */
  specFromId(id, weapon = '1d6+4') {
    const idPtr = alloc(this._m, id);
    const wPtr = alloc(this._m, weapon);
    try {
      const h = this._m._rpg_os_spec_from_id(this._ptr, idPtr.ptr, wPtr.ptr);
      if (!h) throw new Error(`unknown combatant: "${id}"`);
      return this._trackSpec(h);
    } finally {
      free(this._m, idPtr.ptr);
      free(this._m, wPtr.ptr);
    }
  }

  /** Builds a combatant from a live entity (any character can fight). */
  specFromEntity(entity, weapon = '1d6+4') {
    if (!entity || !entity._ptr) throw new Error('specFromEntity needs a live Entity');
    const wPtr = alloc(this._m, weapon);
    try {
      const h = this._m._rpg_os_spec_from_entity(this._ptr, entity._ptr, wPtr.ptr);
      if (!h) throw new Error('failed to build a combatant from the entity');
      return this._trackSpec(h);
    } finally {
      free(this._m, wPtr.ptr);
    }
  }

  _trackSpec(h) {
    const spec = new CombatantSpec(this._m, h, () => this._specs.delete(h));
    this._specs.add(h);
    return spec;
  }

  /**
   * Runs one fight between two combatant specs. `options.maxRounds` (default
   * 1000) guards draws; `options.seed` reproduces a fight exactly;
   * `options.useMagic` (default true) lets spellcasters cast. Returns
   * `{ winner_index, rounds, max_lp, remaining_lp, a, b }` — run it in a loop
   * for Monte-Carlo win probabilities.
   */
  fight(specA, specB, options = {}) {
    if (!specA?._ptr || !specB?._ptr) throw new Error('fight needs two combatant specs');
    return jsonResult(
      this._m, this._m._rpg_os_fight, this._ptr, specA._ptr, specB._ptr,
      options.maxRounds ?? 1000, (options.seed ?? 0) >>> 0,
      (options.useMagic ?? true) ? 1 : 0);
  }

  /**
   * Runs one fight and returns the outcome plus its full transcript — the
   * individual dice rolls and stat changes of every round, from the opening
   * initiative roll to the final hit that decided the winner. Same options as
   * {@link fight}; the result additionally carries `hp_pool` and a `log`
   * object (`{ names, max_lp, winner_index, rounds: [{ round, init_stat,
   * init_roll, init_total, goes_first, actions: [{ actor, target, kind,
   * spell, check_dice, is_hit, damage_dice, damage, hp_before, target_hp,
   * cost, resource }] }] }`). The transcript is observation-only: the same
   * seed produces exactly the same fight as {@link fight}.
   */
  fightDetail(specA, specB, options = {}) {
    if (!specA?._ptr || !specB?._ptr) throw new Error('fightDetail needs two combatant specs');
    return jsonResult(
      this._m, this._m._rpg_os_fight_detail, this._ptr, specA._ptr, specB._ptr,
      options.maxRounds ?? 1000, (options.seed ?? 0) >>> 0,
      (options.useMagic ?? true) ? 1 : 0);
  }

  // ---- lifecycle -----------------------------------------------------------

  /** Releases the engine and every entity/spec it created. */
  dispose() {
    for (const h of this._entities) this._m._rpg_os_entity_free(h);
    for (const h of this._specs) this._m._rpg_os_spec_free(h);
    this._entities.clear();
    this._specs.clear();
    if (this._ptr) {
      this._m._rpg_os_engine_free(this._ptr);
      this._ptr = null;
    }
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
  return new RpgOs(m, enginePtr);
}

export default init;

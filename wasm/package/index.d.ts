// Type definitions for @mundus-mirabilis/rpg-os

/** Metadata of a loaded ruleset, used to build a character entry form. */
export interface RulesetMeta {
  id: string;
  name: string;
  spell_resource: string;
  attributes: Array<{ id: string; name: string; min: number; max: number; default: number }>;
  skills: Array<{ id: string; name: string; default: number; attributes: string[] }>;
  derived_stats: Array<{ id: string; name: string; formula: string }>;
  resource_pools: Array<{ id: string; name: string; max_stat: string; min: number }>;
}

/** A named ruleset entry (archetype or bestiary creature). */
export interface RulesetEntry {
  id: string;
  name: string;
  kind: 'archetypes' | 'creatures';
}

/** Result of resolving a check. */
export interface CheckResult {
  is_success: boolean;
  is_critical_success: boolean;
  is_critical_failure: boolean;
  margin: number;
  remaining_pool: number;
  quality_level: number;
  raw_dice: number[];
}

/** Result of one fight. `winner_index` is -1 on a draw. */
export interface FightOutcome {
  winner_index: number;
  rounds: number;
  max_lp: [number, number];
  remaining_lp: [number, number];
  a: string;
  b: string;
}

export interface CreateEntityOptions {
  /** 0 weakest … 4 strongest, 2 = average (default). */
  variance?: number;
  /** Seed for ranged values; default 1. */
  seed?: number;
}

export interface CheckOptions {
  /** +1 advantage, -1 disadvantage, 0 neutral (default). */
  advantage?: number;
}

export interface FightOptions {
  /** Max rounds before a draw; default 1000. */
  maxRounds?: number;
  /** Seed for a reproducible fight; default 0. */
  seed?: number;
  /** Let spellcasters cast; default true. */
  useMagic?: boolean;
}

/** A live character sheet (wraps a C++ DynamicEntity handle). */
export class Entity {
  setStat(stat: string, value: number): this;
  getStat(stat: string): number;
  getResource(pool: string): number;
  modifyResource(pool: string, delta: number): number;
  refreshResources(): this;
  toJson(): Record<string, unknown>;
  dispose(): void;
}

/** A static combatant description (wraps a C++ CombatantSpec handle). */
export class CombatantSpec {
  dispose(): void;
}

/** The engine: one loaded ruleset and the operations over it. */
export class RpgOs {
  loadRuleset(json: string): this;
  loadRulesetFromFile(filePath: string): this;
  meta(): RulesetMeta;
  entries(): RulesetEntry[];
  createEntity(id: string, options?: CreateEntityOptions): Entity;
  createEntityFromSheet(id: string, stats: Record<string, number>): Entity;
  check(checkType: string, actor: Entity, target?: Entity | null, options?: CheckOptions): CheckResult;
  specFromId(id: string, weapon?: string): CombatantSpec;
  specFromEntity(entity: Entity, weapon?: string): CombatantSpec;
  fight(specA: CombatantSpec, specB: CombatantSpec, options?: FightOptions): FightOutcome;
  dispose(): void;
}

/**
 * Initializes the engine (loads + instantiates the WASM module once).
 * Works in Node and the browser.
 */
export function init(): Promise<RpgOs>;

export default init;

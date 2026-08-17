// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// End-to-end tests for the @mundus-mirabilis/rpg-os WASM package. They run
// against the real built WASM module (wasm/package/dist/) and the shipped
// rulesets, so they validate the whole compile + wrapper pipeline. The parity
// test additionally compares a seeded WASM fight against the native
// rpg_os_example_fight binary (skipped when the binary is not built).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { init } from '../index.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const packageRoot = path.resolve(here, '..');
const repoRoot = path.resolve(packageRoot, '..', '..');
const tdePath = path.join(packageRoot, 'rulesets', 'tde5e_core.json');
const dndPath = path.join(packageRoot, 'rulesets', 'dnd5e_srd.json');
const nativeBinary = path.join(repoRoot, 'build', 'bin', 'rpg_os_example_fight');

let rpg;
test.before(async () => {
  rpg = await init();
});

test.after(() => {
  rpg?.dispose();
});

test('loads the TDE ruleset and exposes its metadata', () => {
  rpg.loadRulesetFromFile(tdePath);
  const meta = rpg.meta();
  assert.equal(meta.id, 'tde5e_core');
  assert.ok(meta.attributes.some((a) => a.id === 'COU'));
  assert.ok(meta.resource_pools.some((p) => p.id === 'LP'));
  const entries = rpg.entries();
  assert.ok(entries.some((e) => e.id === 'geron' && e.kind === 'archetypes'));
  assert.ok(entries.some((e) => e.id === 'gotongi' && e.kind === 'creatures'));
});

test('creates an entity from a named archetype (geron)', () => {
  const geron = rpg.createEntity('geron');
  assert.equal(geron.getStat('COU'), 12);
  assert.equal(geron.getStat('Attack'), 7);
  assert.equal(geron.getResource('LP'), 31); // 5 + 2*CON (CON 13)
});

test('creates an entity from an arbitrary sheet (no ruleset entry)', () => {
  const hero = rpg.createEntityFromSheet('my_hero', {
    COU: 14, AGI: 15, CON: 13, Attack: 12, Parry: 8, Armor_Rating: 3, Initiative: 12,
  });
  assert.equal(hero.getStat('Attack'), 12);
  assert.equal(hero.getStat('COU'), 14);
  assert.equal(hero.getResource('LP'), 31); // pools are derived from the sheet
  const sheet = hero.toJson();
  assert.equal(sheet.stats.Attack, 12);
});

test('resolves a check against a target', () => {
  const hero = rpg.createEntityFromSheet('my_hero', {
    COU: 14, AGI: 15, CON: 13, Attack: 12, Parry: 8, Armor_Rating: 3, Initiative: 12,
  });
  const toad = rpg.createEntity('toad');
  const result = rpg.check('tde_attack', hero, toad);
  assert.equal(typeof result.is_success, 'boolean');
  assert.ok(Array.isArray(result.raw_dice));
  assert.ok(result.raw_dice.length >= 2); // attack + parry rolls
});

test('a fight is reproducible for a fixed seed', () => {
  const geronA = rpg.specFromId('geron');
  const toadA = rpg.specFromId('toad');
  const geronB = rpg.specFromId('geron');
  const toadB = rpg.specFromId('toad');
  const first = rpg.fight(geronA, toadA, { seed: 12345, maxRounds: 1000 });
  const second = rpg.fight(geronB, toadB, { seed: 12345, maxRounds: 1000 });
  assert.deepEqual(first, second);
});

test('a hand-built character beats a toad (arbitrary sheet fights)', () => {
  const hero = rpg.createEntityFromSheet('my_hero', {
    COU: 14, AGI: 15, CON: 13, Attack: 12, Parry: 8, Armor_Rating: 3, Initiative: 12,
  });
  const heroSpec = rpg.specFromEntity(hero);
  const toadSpec = rpg.specFromId('toad');
  let heroWins = 0;
  for (let i = 0; i < 20; i += 1) {
    const outcome = rpg.fight(heroSpec, toadSpec, { seed: i, maxRounds: 100 });
    if (outcome.winner_index === 0) heroWins += 1;
  }
  assert.ok(heroWins >= 19, `hero should beat the toad ~always, won ${heroWins}/20`);
});

test('parity: seeded WASM fight matches the native rpg_os_example_fight binary', { skip: !existsSync(nativeBinary) }, () => {
  rpg.loadRulesetFromFile(tdePath);
  const seed = 12345;
  const nativeOut = execFileSync(nativeBinary, [
    '--root', repoRoot, '--ruleset', tdePath, '--csv', '--batch', '1', '--rounds', '1000',
    '--seed', String(seed), 'geron', 'toad',
  ], { encoding: 'utf8' });
  const fields = nativeOut.trim().split('\t');
  assert.equal(fields.length, 5, `unexpected native CSV: ${nativeOut.trim()}`);
  const [winnerId, , winnerLp, , rounds] = fields;

  const geron = rpg.specFromId('geron');
  const toad = rpg.specFromId('toad');
  const outcome = rpg.fight(geron, toad, { seed, maxRounds: 1000 });

  const expectedWinner = winnerId === 'DRAW' ? -1 : winnerId === 'geron' ? 0 : 1;
  assert.equal(outcome.winner_index, expectedWinner, 'winner index matches native');
  assert.equal(outcome.rounds, Number(rounds), 'rounds match native');
  const expectedWinnerLp = expectedWinner === 0 ? outcome.remaining_lp[0] : outcome.remaining_lp[1];
  assert.equal(expectedWinnerLp, Number(winnerLp), 'winner LP matches native');
});

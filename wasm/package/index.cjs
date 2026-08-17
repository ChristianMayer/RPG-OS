// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

'use strict';

// CommonJS entry point. The WASM glue is an ES module (built with
// EXPORT_ES6), so `require()` users delegate through a dynamic import.
// Every API is reached through `init()`:
//
//   const { init } = require('@mundus-mirabilis/rpg-os');
//   const rpg = await init();
//   rpg.loadRuleset(require('node:fs').readFileSync('rulesets/tde5e_core.json', 'utf8'));

module.exports = {
  init: (...args) => import('./index.mjs').then((m) => m.init(...args)),
};

module.exports.default = module.exports;

#!/usr/bin/env python3
# Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
# SPDX-License-Identifier: Apache-2.0

"""Monte Carlo ELO ranking of every combatant in a ruleset.

Runs a Swiss-style tournament on top of the fight example binary
(`rpg_os_example_fight`, --csv mode). In every round the combatants are sorted
by their current ELO rating and each entry plays one opponent of similar
strength; the pair plays `--games` Monte Carlo fights (dice are re-rolled every
fight) and the ratings are updated with the standard ELO formula afterwards.

This is deliberately NOT a full round-robin: with `n` entries a Swiss
tournament needs O(rounds * n) fights instead of O(n^2), so it scales to large
bestiaries while still converging to a meaningful ranking. The individual
fight simulations run in `--jobs` parallel processes.

Usage:
    python3 scripts/elo_ranking.py [options]

Examples:
    # Rank all TDE combatants with the default settings (8 parallel fights).
    python3 scripts/elo_ranking.py --jobs 8

    # A quick 10-round run with 5 Monte Carlo fights per pair.
    python3 scripts/elo_ranking.py --rounds 10 --games 5
"""

import argparse
import concurrent.futures
import json
import os
import secrets
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_BINARY = os.path.join(REPO_ROOT, "build", "bin", "rpg_os_example_fight")
DEFAULT_RULESET = os.path.join(REPO_ROOT, "rulesets", "tde5e_core.json")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Monte Carlo ELO ranking of a ruleset's combatants (Swiss style).",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--binary", default=DEFAULT_BINARY,
                        help="path to the rpg_os_example_fight binary")
    parser.add_argument("--ruleset", default=DEFAULT_RULESET,
                        help="ruleset JSON file to rank")
    parser.add_argument("--rounds", type=int, default=40,
                        help="Swiss tournament rounds (each combatant plays one "
                             "opponent per round)")
    parser.add_argument("--games", type=int, default=10,
                        help="Monte Carlo fights per pair per round")
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1),
                        help="parallel fight processes")
    parser.add_argument("--max-rounds", type=int, default=1000,
                        help="max rounds of a single fight (draw guard)")
    parser.add_argument("--k", type=float, default=32.0, help="ELO K-factor")
    parser.add_argument("--initial", type=float, default=1500.0,
                        help="starting ELO rating for every combatant")
    parser.add_argument("--seed", type=int, default=None,
                        help="seed for reproducible pairing and fight seeds "
                             "(default: fresh entropy)")
    parser.add_argument("--list", action="store_true",
                        help="list all combatants in the ruleset and exit")
    return parser.parse_args()


def load_entries(ruleset_path):
    """Returns the ids of all archetypes and creatures in the ruleset data."""
    with open(ruleset_path, "r", encoding="utf-8") as handle:
        ruleset = json.load(handle)
    data = ruleset.get("data", {})
    entries = []
    for section in ("archetypes", "creatures"):
        entries.extend(entry["id"] for entry in data.get(section, []))
    return entries


def run_pair_job(binary, root, ruleset, a, b, games, max_rounds, seed):
    """Runs `games` fights between `a` and `b` in one fight process.

    Returns (wins_a, wins_b, draws) as counted from the CSV lines. Each fight
    prints one line: `<winner_id>\\t<loser_id>\\t...` or `DRAW\\t...`.
    """
    cmd = [binary, "--root", root, "--ruleset", ruleset, "--csv",
           "--batch", str(games), "--rounds", str(max_rounds),
           "--seed", str(seed), a, b]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        detail = (proc.stderr or proc.stdout).strip()
        raise RuntimeError(f"fight simulation failed for {a} vs {b}: {detail}")

    wins_a = wins_b = draws = 0
    for line in proc.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        winner = line.split("\t", 1)[0]
        if winner == a:
            wins_a += 1
        elif winner == b:
            wins_b += 1
        elif winner == "DRAW":
            draws += 1
    if wins_a + wins_b + draws != games:
        raise RuntimeError(
            f"expected {games} fight lines for {a} vs {b}, got "
            f"{wins_a + wins_b + draws}")
    return wins_a, wins_b, draws


def expected(rating_a, rating_b):
    """Expected score of `a` against `b` (0..1)."""
    return 1.0 / (1.0 + 10.0 ** ((rating_b - rating_a) / 400.0))


def update_elo(ratings, stats, a, b, wins_a, wins_b, draws, k):
    """One batch ELO update after `n` games between `a` and `b`."""
    total = wins_a + wins_b + draws
    if total == 0:
        return
    score_a = (wins_a + 0.5 * draws) / total
    expected_a = expected(ratings[a], ratings[b])
    ratings[a] += k * (score_a - expected_a)
    ratings[b] += k * ((1.0 - score_a) - (1.0 - expected_a))

    for entry, wins, losses in ((a, wins_a, wins_b), (b, wins_b, wins_a)):
        stats[entry]["games"] += total
        stats[entry]["wins"] += wins
        stats[entry]["losses"] += losses
        stats[entry]["draws"] += draws


def build_pairs(entries, ratings, round_index, seed):
    """Swiss pairing: sort by rating, rotate by round, pair adjacent.

    Each entry plays exactly one opponent per round, so a tournament of
    `rounds` rounds needs only O(rounds * n) fights. Returns (pairs, bye).
    """
    order = sorted(entries, key=lambda entry: ratings[entry], reverse=True)
    shift = (round_index + seed) % len(order)
    order = order[shift:] + order[:shift]
    pairs = [(order[i], order[i + 1]) for i in range(0, len(order) - 1, 2)]
    bye = order[-1] if len(order) % 2 == 1 else None
    return pairs, bye


def print_ranking(ratings, stats, entries):
    ranked = sorted(entries, key=lambda entry: ratings[entry], reverse=True)
    header = f"{'#':>3}  {'rating':>7}  {'games':>5}  {'wins':>5}  {'draws':>5}  " \
             f"{'losses':>6}  {'win%':>6}  combatant"
    print("\n" + header)
    print("-" * len(header))
    for index, entry in enumerate(ranked, start=1):
        stat = stats[entry]
        win_pct = 100.0 * stat["wins"] / stat["games"] if stat["games"] else 0.0
        print(f"{index:>3}  {ratings[entry]:>7.1f}  {stat['games']:>5}  "
              f"{stat['wins']:>5}  {stat['draws']:>5}  {stat['losses']:>6}  "
              f"{win_pct:>6.1f}  {entry}")
    print(f"\nChampion: {ranked[0]} (rating {ratings[ranked[0]]:.1f})")


def _write_progress(done, total, width=30):
    """Draws an in-place progress bar on stderr; call again to update."""
    if total <= 0:
        return
    filled = width * done // total
    bar = "#" * filled + "-" * (width - filled)
    percent = 100.0 * done / total
    sys.stderr.write(f"\rMonte Carlo ELO: [{bar}] round {done}/{total} "
                     f"({percent:5.1f}%)")
    sys.stderr.flush()


def main():
    args = parse_args()
    entries = load_entries(args.ruleset)
    if not entries:
        print(f"no combatants found in {args.ruleset}", file=sys.stderr)
        return 1
    if args.list:
        print("\n".join(entries))
        return 0
    if len(entries) < 2:
        print("need at least two combatants to run a tournament", file=sys.stderr)
        return 1
    if not os.path.isfile(args.binary):
        print(f"fight binary not found: {args.binary}\n"
              f"Build it first: cmake --build build --target rpg_os_example_fight",
              file=sys.stderr)
        return 1

    # No explicit --seed: draw a fresh random one and report it, so a run
    # can be reproduced later by passing it back.
    if args.seed is None:
        args.seed = secrets.randbits(32)
        print(f"no --seed given: using random seed {args.seed} "
              f"(pass --seed {args.seed} to reproduce this run)", file=sys.stderr)

    ratings = {entry: float(args.initial) for entry in entries}
    stats = {entry: {"games": 0, "wins": 0, "draws": 0, "losses": 0}
             for entry in entries}

    print(f"Monte Carlo ELO ranking: {len(entries)} combatants, "
          f"{args.rounds} rounds, {args.games} games/pair, "
          f"{args.jobs} parallel jobs, K={args.k:g}", file=sys.stderr)

    # Only an interactive terminal gets the animated bar; when stderr is piped
    # (e.g. to a log) keep the output line-oriented and skip per-round spam.
    use_progress = sys.stderr.isatty()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for round_index in range(1, args.rounds + 1):
            pairs, _bye = build_pairs(entries, ratings, round_index, args.seed)
            futures = {}
            for pair_index, (a, b) in enumerate(pairs):
                seed = (args.seed * 1000003 + round_index * 1009 + pair_index * 31) & 0xFFFFFFFF
                future = pool.submit(run_pair_job, args.binary, REPO_ROOT,
                                     args.ruleset, a, b, args.games,
                                     args.max_rounds, seed)
                futures[future] = (a, b)
            for future in concurrent.futures.as_completed(futures):
                a, b = futures[future]
                update_elo(ratings, stats, a, b, *future.result(), args.k)
            if use_progress:
                _write_progress(round_index, args.rounds)
    if use_progress:
        sys.stderr.write("\n")

    print_ranking(ratings, stats, entries)
    return 0


if __name__ == "__main__":
    sys.exit(main())

// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file dice_engine.hpp
 * @ingroup rpg_os_core
 * @brief Dice expression parsing and rolling.
 *
 * Parses dice expressions such as @c "1d20", @c "3d20", @c "2d6+4",
 * @c "1d20-2" or @c "2d6+1d4" into a list of die groups plus a flat constant,
 * and rolls them with an injectable random number generator (see
 * @c core/concepts.hpp). Shared by the universal engine and the generated
 * specific-mode code.
 *
 * @par Why parse dice expressions at all?
 * Rulesets express damage and hit points as dice expressions verbatim from
 * the source books (@c "2d6+4"). Keeping them as text in the JSON — rather
 * than pre-rolling or pre-computing bounds — preserves the original stat and
 * lets the engine roll at the right moment with the right variance. This
 * header turns that text into a structure that can be rolled, bounded (see
 * @c variance.hpp), and validated.
 *
 * @par Dice as literals
 * Once a dice string is parsed it is a plain C++ object. Two literal forms
 * build one directly from source text: the common die-size suffixes
 * (@c 3_d20, and with the arithmetic operators @c 2_d6 + 2) for everyday
 * use, and the @c "2d6+4"_dice suffix for arbitrary expressions — the form
 * the generated ruleset headers use, since a ruleset JSON may contain any
 * dice string. Either way the dice are parsed once at startup instead of
 * re-interpreting a string on every roll.
 *
 * @par Why injectable randomness?
 * Dice are consumed by check algorithms, damage rolls, and entity creation.
 * Injecting an RNG (rather than having @c DiceExpression own one) keeps the
 * engine deterministic under a scripted RNG in tests and reproducible under a
 * seeded RNG in Monte-Carlo simulations — the same expression object can be
 * rolled by many independent RNGs.
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <rpg_os/core/concepts.hpp>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg_os {

/// Returns a 32-bit seed drawn from easily available entropy: the OS random
/// device when present, falling back to the high-resolution clock.
///
/// @par Why such a simple seed?
/// This is a game engine, not a cryptographic library. An unseeded run only
/// needs to *differ from other runs*, which a single word from the OS random
/// device guarantees; if no device exists the clock still changes every run.
/// Callers who want stronger or exactly reproducible randomness pass an
/// explicit seed to `DefaultRandom` — and `randomSeed()` is the capture point
/// for replaying a run later (`--seed N`).
[[nodiscard]] inline uint32_t randomSeed() noexcept {
  try {
    std::random_device rd;
    return rd();
  } catch (...) {
    // No OS entropy device: the high-resolution clock still differs between
    // runs, which is all an unseeded game run needs.
    return static_cast<uint32_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
  }
}

/**
 * Default RNG: a std::mt19937 wrapped in a uniform [min, max] distribution.
 * Satisfies the @c RandomNumberGenerator concept.
 *
 * @par Why std::mt19937?
 * The Mersenne Twister is the standard "good enough" engine: fast, portable,
 * and deterministic for a fixed seed, which is exactly the split this library
 * needs — seeded instances replay a run bit-for-bit, unseeded instances draw
 * from easily available entropy.
 *
 * @par Seeding strategy
 * Default-constructed instances seed from @ref randomSeed (the OS random
 * device, falling back to the clock), so unseeded runs differ every time —
 * good enough for a game, and trivially upgraded by passing an explicit seed
 * for reproducible simulations or tests.
 */
class DefaultRandom {
public:
  /// Unseeded: seeds the engine from easily available entropy (see
  /// @ref randomSeed), so every run differs.
  DefaultRandom() : m_engine(randomSeed()) {}

  /// Deterministic and reproducible for a fixed `seed`.
  explicit DefaultRandom(uint32_t seed) : m_engine(seed) {}

  /// Returns a uniform integer in [min, max] (inclusive). The operator form
  /// is what satisfies the @c RandomNumberGenerator concept.
  ///
  /// @par Why not std::uniform_int_distribution?
  /// `std::uniform_int_distribution` is *not* portable: for the same engine
  /// and seed it produces different sequences on libstdc++ and libc++. The
  /// engine here draws from the raw @c std::mt19937 output instead, which is
  /// specified by the standard and identical on every implementation — so
  /// "explicitly seeded = exactly reproducible" also holds across a native
  /// build (libstdc++ or libc++) and the WebAssembly build (libc++). That
  /// cross-standard-library determinism is what lets the npm package's
  /// parity tests compare a seeded WASM fight with the native fight binary.
  /// The tiny modulo bias is irrelevant for a game RNG.
  int operator()(int min, int max) {
    const uint32_t span = static_cast<uint32_t>(max) - static_cast<uint32_t>(min) + 1u;
    return min + static_cast<int>(m_engine() % span);
  }

private:
  std::mt19937 m_engine;
};

/**
 * One group of identical dice within an expression, e.g. the "2d6" part of
 * "2d6+4".
 *
 * @par Why a sign per group?
 * Expressions like @c "1d6-1d6" appear in real source material. Splitting the
 * sign out of @ref count (instead of negating the count) keeps the raw dice
 * results positive in roll order — important for checks that inspect
 * individual dice — while @ref DiceExpression::rollSum applies the sign only
 * to the sum.
 */
struct DieSpec {
  int count{1};
  int sides{20};
  int sign{1}; ///< +1 or -1; a negative sign applies to the rolled sum only.
};

/**
 * A parsed dice expression. Immutable after construction.
 *
 * @par Why immutable?
 * Once parsed, a dice expression is pure data; making it immutable lets it be
 * safely shared (the combat loop holds one per combatant across many fights)
 * and lets the compiler reason about it. Parsing happens eagerly in the
 * constructor so a malformed expression from a ruleset fails fast at load
 * time rather than mid-roll.
 */
class DiceExpression {
public:
  /// Parses `expression`; throws std::invalid_argument on malformed input.
  constexpr explicit DiceExpression(std::string_view expression);

  /// Builds a constant-only expression (no dice, a flat value), equivalent to
  /// @c DiceExpression("7"). Used by the @c 7_dice integer literal.
  constexpr explicit DiceExpression(int constant) : m_constant(constant) {}

  /// Builds a single group of `count` dice with `sides` faces (e.g.
  /// @c DiceExpression(3, 20) == @c DiceExpression("3d20")). Used by the
  /// @c 3_d20 die-size literals; throws std::invalid_argument if either value
  /// is less than one.
  constexpr explicit DiceExpression(int count, int sides) {
    if (count < 1) {
      throw std::invalid_argument("die count must be positive");
    }
    if (sides < 1) {
      throw std::invalid_argument("die sides must be positive");
    }
    m_dice.push_back(DieSpec{count, sides});
  }

  /// The die groups in order of appearance.
  [[nodiscard]] constexpr const std::vector<DieSpec> &dice() const noexcept {
    return m_dice;
  }

  /// The total number of individual dice rolled by the expression (the sum
  /// of every group's count). Used by the ruleset loader to validate that a
  /// pool check has exactly one die per attribute.
  [[nodiscard]] constexpr std::size_t dieCount() const noexcept {
    return totalDieCount();
  }

  /// The flat additive constant (already includes its sign).
  [[nodiscard]] constexpr int constant() const noexcept {
    return m_constant;
  }

  /// The expected (average) total of the expression: the constant plus, per
  /// group, `sign * count * (sides + 1) / 2`. This is the flat mathematical
  /// mean of all outcomes — useful for ranking options (e.g. a mage picking
  /// the strongest spell to cast) without actually rolling dice.
  [[nodiscard]] constexpr double expectedValue() const noexcept {
    double total = static_cast<double>(m_constant);
    for (const DieSpec &die : m_dice) {
      total += die.sign * die.count * (die.sides + 1) / 2.0;
    }
    return total;
  }

  /// Rolls every die with `rng`. The raw results are always positive and
  /// appear in roll order (checks that need individual dice use this).
  template <RandomNumberGenerator Rng>
  [[nodiscard]] constexpr std::vector<int> roll(Rng &rng) const {
    std::vector<int> results;
    results.reserve(totalDieCount());
    for (const DieSpec &die : m_dice) {
      for (int i = 0; i < die.count; ++i) {
        results.push_back(rng(1, die.sides));
      }
    }
    return results;
  }

  /// Rolls every die and returns the signed total (constant + signed dice).
  /// This is the common "how much damage / how high did I roll" entry point;
  /// use @ref roll instead when individual dice matter.
  template <RandomNumberGenerator Rng> [[nodiscard]] constexpr int rollSum(Rng &rng) const {
    int sum = m_constant;
    for (const DieSpec &die : m_dice) {
      for (int i = 0; i < die.count; ++i) {
        sum += die.sign * rng(1, die.sides);
      }
    }
    return sum;
  }

  /// Unary minus: negates the constant and every die group's sign, so
  /// @c -(2_d6) is the same expression as the "-2d6" term of a parsed string
  /// (raw rolls stay positive; only the sum is negated).
  [[nodiscard]] constexpr DiceExpression operator-() const {
    std::vector<DieSpec> negated = m_dice;
    for (DieSpec &die : negated) {
      die.sign = -die.sign;
    }
    return DiceExpression(std::move(negated), -m_constant);
  }

  /// Combines two expressions: their die groups are concatenated and their
  /// constants added, so @c 2_d6 + 1_d4 is one two-group expression.
  [[nodiscard]] constexpr DiceExpression operator+(const DiceExpression &rhs) const {
    std::vector<DieSpec> combined = m_dice;
    combined.insert(combined.end(), rhs.m_dice.begin(), rhs.m_dice.end());
    return DiceExpression(std::move(combined), m_constant + rhs.m_constant);
  }

  /// Subtracts `rhs`: its die groups and constant are negated first, e.g.
  /// @c 3_d6 - 1_d4.
  [[nodiscard]] constexpr DiceExpression operator-(const DiceExpression &rhs) const {
    return *this + (-rhs);
  }

  /// Adds a flat constant, e.g. @c 2_d6 + 2.
  [[nodiscard]] constexpr DiceExpression operator+(int rhs) const {
    return DiceExpression(m_dice, m_constant + rhs);
  }

  /// Subtracts a flat constant, e.g. @c 1_d20 - 2.
  [[nodiscard]] constexpr DiceExpression operator-(int rhs) const {
    return *this + (-rhs);
  }

private:
  /// Raw construction used by the arithmetic operators; takes ownership of
  /// `dice` and the combined constant.
  constexpr DiceExpression(std::vector<DieSpec> dice, int constant)
      : m_dice(std::move(dice)), m_constant(constant) {}

  [[nodiscard]] constexpr std::size_t totalDieCount() const noexcept {
    std::size_t n = 0;
    for (const DieSpec &die : m_dice) {
      n += static_cast<std::size_t>(die.count);
    }
    return n;
  }

  constexpr void parse(std::string_view expression) {
    if (expression.empty()) {
      throw std::invalid_argument("empty dice expression");
    }
    std::size_t i = 0;
    while (i < expression.size()) {
      if (expression[i] == ' ') {
        ++i;
        continue;
      }
      int sign = 1;
      if (expression[i] == '+') {
        ++i;
      } else if (expression[i] == '-') {
        sign = -1;
        ++i;
      }
      int number = 0;
      bool hasNumber = false;
      while (i < expression.size() && expression[i] >= '0' && expression[i] <= '9') {
        number = number * 10 + (expression[i] - '0');
        hasNumber = true;
        ++i;
      }
      if (i < expression.size() && (expression[i] == 'd' || expression[i] == 'D')) {
        ++i; // consume 'd'
        if (i >= expression.size()) {
          throw std::invalid_argument("missing die sides in dice expression");
        }
        int sides = 0;
        while (i < expression.size() && expression[i] >= '0' && expression[i] <= '9') {
          sides = sides * 10 + (expression[i] - '0');
          ++i;
        }
        if (sides <= 0) {
          throw std::invalid_argument("die sides must be positive");
        }
        const int count = hasNumber ? number : 1; // "d20" == "1d20"
        if (count <= 0) {
          throw std::invalid_argument("die count must be positive");
        }
        m_dice.push_back(DieSpec{count, sides, sign});
      } else {
        if (!hasNumber) {
          throw std::invalid_argument("unexpected character in dice expression");
        }
        m_constant += sign * number;
      }
    }
  }

  std::vector<DieSpec> m_dice;
  int m_constant{0};
};

constexpr DiceExpression::DiceExpression(std::string_view expression) {
  parse(expression);
}

/// @name arithmetic with a leading integer
/// Allow @c 2 + 2_d6 and @c 2 - 2_d6; found by ADL on @c DiceExpression.
/// @{

/// Adds a flat constant on the left: @c 2 + 2_d6 == @c 2_d6 + 2.
[[nodiscard]] constexpr DiceExpression operator+(int lhs, const DiceExpression &rhs) {
  return rhs + lhs;
}

/// Subtracts an expression from a constant: @c 2 - 2_d6 keeps the constant 2
/// and rolls the dice subtracted.
[[nodiscard]] constexpr DiceExpression operator-(int lhs, const DiceExpression &rhs) {
  return (-rhs) + lhs;
}

/// @}

/// User-defined literal operators for dice expressions.
///
/// Two families are provided:
/// - the @c "2d6+4"_dice suffix for arbitrary expressions — the form the
///   generated ruleset headers use, since a ruleset JSON may contain any
///   dice string;
/// - the common die-size suffixes @c _d2, @c _d3, @c _d4, @c _d6, @c _d8,
///   @c _d10, @c _d12, @c _d20, @c _d30 and @c _d100, where the integer part
///   is the number of dice: @c 3_d20 is three D20, and with the arithmetic
///   operators on @c DiceExpression, @c 2_d6 + 2 builds one expression that
///   rolls two D6 plus two.
///
/// @par Why an underscore before the die size?
/// All suffixes begin with an underscore (like @c _d20), the fully conforming
/// form every compiler accepts. A bare suffix such as @c 3d20 is rejected by
/// Clang's lexer ("invalid digit 'd' in decimal constant") and reserved by
/// the standard, so the underscore is the portable way to write "3 d20".
///
/// @par Methods on a bare literal
/// A die-size literal like @c 1_d20 is a numeric literal, so a method call
/// written directly after it — @c 1_d20.rollSum(rng) — would be swallowed
/// into the literal token ('.' continues a preprocessing number). Bind it to
/// a variable or parenthesise: @c auto d = 1_d20; d.rollSum(rng) or
/// @c (1_d20).rollSum(rng).
inline namespace dice_literals {

/// Parses an arbitrary dice expression string, e.g. @c "2d6+4"_dice.
[[nodiscard]] constexpr DiceExpression operator""_dice(const char *str, std::size_t len) {
  return DiceExpression(std::string_view(str, len));
}

/// Constant-only integer literal: @c 7_dice is a flat 7.
[[nodiscard]] constexpr DiceExpression operator""_dice(unsigned long long value) {
  return DiceExpression(static_cast<int>(value));
}

/// @name common die sizes — the integer part is the number of dice
/// @{
[[nodiscard]] constexpr DiceExpression operator""_d2(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 2);
}
[[nodiscard]] constexpr DiceExpression operator""_d3(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 3);
}
[[nodiscard]] constexpr DiceExpression operator""_d4(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 4);
}
[[nodiscard]] constexpr DiceExpression operator""_d6(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 6);
}
[[nodiscard]] constexpr DiceExpression operator""_d8(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 8);
}
[[nodiscard]] constexpr DiceExpression operator""_d10(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 10);
}
[[nodiscard]] constexpr DiceExpression operator""_d12(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 12);
}
[[nodiscard]] constexpr DiceExpression operator""_d20(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 20);
}
[[nodiscard]] constexpr DiceExpression operator""_d30(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 30);
}
[[nodiscard]] constexpr DiceExpression operator""_d100(unsigned long long count) {
  return DiceExpression(static_cast<int>(count), 100);
}
/// @}

} // namespace dice_literals

} // namespace rpg_os

// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

// Dice expression parsing and rolling.
//
// Parses dice expressions such as "1d20", "3d20", "2d6+4", "1d20-2" or
// "2d6+1d4" into a list of die groups plus a flat constant, and rolls them
// with an injectable random number generator (see core/concepts.hpp). Shared
// by the universal engine and the generated specific-mode code.
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <rpg_os/core/concepts.hpp>
#include <stdexcept>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace rpg_os {

namespace detail {

/// The process id, used as one entropy source (0 when unavailable).
[[nodiscard]] inline uint32_t processId() noexcept {
#if defined(_WIN32)
  return static_cast<uint32_t>(_getpid());
#elif defined(__unix__) || defined(__APPLE__)
  return static_cast<uint32_t>(getpid());
#else
  return 0u;
#endif
}

/// Collects 8 entropy words from the OS random device (when available), mixed
/// with the high-resolution clock, the process id and an ASLR-random stack
/// address. Stays genuinely random even where `std::random_device` is weak or
/// deterministic (e.g. some MinGW builds).
[[nodiscard]] inline std::array<uint32_t, 8> entropyWords() noexcept {
  std::array<uint32_t, 8> words{};
  try {
    std::random_device rd;
    for (std::size_t i = 0; i < words.size(); ++i) {
      words[i] = rd();
    }
  } catch (...) {
    // No OS entropy source available: the other sources mixed in below still
    // provide enough variety between processes / runs.
  }
  const std::uint64_t now = static_cast<std::uint64_t>(
      std::chrono::high_resolution_clock::now().time_since_epoch().count());
  const std::uintptr_t stack = reinterpret_cast<std::uintptr_t>(&now);
  words[0] ^= static_cast<uint32_t>(now);
  words[1] ^= static_cast<uint32_t>(now >> 32);
  words[2] ^= static_cast<uint32_t>(stack);
  words[3] ^= processId();
  return words;
}

} // namespace detail

/// Returns a 32-bit seed gathered from genuine entropy: the OS random device
/// (when available) mixed with the high-resolution clock, the process id and
/// an ASLR-random stack address. Use it whenever a "really random" seed is
/// needed; explicit seeds passed to `DefaultRandom` keep runs reproducible.
[[nodiscard]] inline uint32_t randomSeed() noexcept {
  const std::array<uint32_t, 8> words = detail::entropyWords();
  uint32_t result = 0;
  for (const uint32_t word : words) {
    result ^= word;
  }
  return result;
}

/// Default RNG: a std::mt19937 wrapped in a uniform [min, max] distribution.
/// Satisfies the RandomNumberGenerator concept.
///
/// Default-constructed instances are seeded from genuine OS entropy, so
/// unseeded runs differ every time. Constructing with an explicit seed is
/// deterministic and reproducible (tests, Monte Carlo simulations).
class DefaultRandom {
public:
  /// Truly random: seeds the engine from OS entropy (see `randomSeed`).
  DefaultRandom() {
    const std::array<uint32_t, 8> words = detail::entropyWords();
    std::seed_seq seq(words.begin(), words.end());
    m_engine.seed(seq);
  }

  /// Deterministic and reproducible for a fixed `seed`.
  explicit DefaultRandom(uint32_t seed) : m_engine(seed) {}

  /// Returns a uniform integer in [min, max] (inclusive).
  int operator()(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(m_engine);
  }

private:
  std::mt19937 m_engine;
};

/// One group of identical dice within an expression, e.g. the "2d6" part of
/// "2d6+4".
struct DieSpec {
  int count{1};
  int sides{20};
  int sign{1}; ///< +1 or -1; a negative sign applies to the rolled sum only.
};

/// A parsed dice expression. Immutable after construction.
class DiceExpression {
public:
  /// Parses `expression`; throws std::invalid_argument on malformed input.
  explicit DiceExpression(std::string_view expression);

  /// The die groups in order of appearance.
  [[nodiscard]] const std::vector<DieSpec> &dice() const noexcept {
    return m_dice;
  }

  /// The flat additive constant (already includes its sign).
  [[nodiscard]] int constant() const noexcept {
    return m_constant;
  }

  /// Rolls every die with `rng`. The raw results are always positive and
  /// appear in roll order (checks that need individual dice use this).
  template <RandomNumberGenerator Rng> [[nodiscard]] std::vector<int> roll(Rng &rng) const {
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
  template <RandomNumberGenerator Rng> [[nodiscard]] int rollSum(Rng &rng) const {
    int sum = m_constant;
    for (const DieSpec &die : m_dice) {
      for (int i = 0; i < die.count; ++i) {
        sum += die.sign * rng(1, die.sides);
      }
    }
    return sum;
  }

private:
  [[nodiscard]] std::size_t totalDieCount() const noexcept {
    std::size_t n = 0;
    for (const DieSpec &die : m_dice) {
      n += static_cast<std::size_t>(die.count);
    }
    return n;
  }

  void parse(std::string_view expression) {
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
        const int count = hasNumber ? number : 1;
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

inline DiceExpression::DiceExpression(std::string_view expression) {
  parse(expression);
}

} // namespace rpg_os

// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file money.hpp
 * @brief Generic currency model (denominations + an amount).
 *
 * Tabletop games express wealth in coins of different value (D&D's copper /
 * silver / gold / platinum, The Dark Eye's Heller / Kreutzer / Silbertaler /
 * Dukat). @ref CurrencySystem describes one game's coinage — the base unit
 * and each denomination's value in base units; @ref Money is an amount stored
 * in base units so arithmetic never cares about the coinage. A ruleset that
 * does not mention money simply has no @c currencies section, in which case
 * @c Money is never used.
 */
#pragma once

#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg_os {

/// One coin type: a named denomination with a fixed value in base units.
struct Denomination {
  std::string id;     ///< machine id, e.g. "gp"
  std::string name;   ///< human name, e.g. "Gold"
  std::string symbol; ///< printed abbreviation, e.g. "GP"
  int64_t perBase{1}; ///< how many base units one of this coin is worth
};

/// The coinage of one ruleset: a base unit plus the denominations.
///
/// @par Why "perBase" on each denomination?
/// It is the only number the engine needs: a conversion, a price, or a wealth
/// figure is always stored in base units, and any denomination is that many
/// base units. Exchange between coins is pure arithmetic on the base-unit
/// amount.
struct CurrencySystem {
  std::string id;       ///< e.g. "dnd_coins"
  std::string name;     ///< e.g. "D&D Coins"
  std::string baseUnit; ///< the smallest coin, e.g. "cp"
  std::vector<Denomination> denominations;

  /// Looks up a denomination by id; nullptr when unknown.
  [[nodiscard]] const Denomination *find(std::string_view denomId) const {
    for (const Denomination &d : denominations) {
      if (d.id == denomId) {
        return &d;
      }
    }
    return nullptr;
  }

  /// Value in base units of `qty` of `denomId`; 0 when the denomination is
  /// unknown (so a ruleset that dropped a coin degrades to "worthless").
  [[nodiscard]] int64_t valueOf(std::string_view denomId, int64_t qty) const {
    const Denomination *d = find(denomId);
    return d == nullptr ? 0 : d->perBase * qty;
  }
};

/// An amount of money, always stored in the currency's base units.
///
/// @par Why store base units instead of a coin map?
/// A coin map makes arithmetic awkward (how do you add two bags?). A single
/// base-unit integer keeps + - * and comparison trivial and exact, and the
/// coinage is only consulted when you want a human-readable breakdown
/// (@ref in or @ref fromCoins). This is the same "store the canonical value,
/// render it on demand" choice the engine makes for stats.
class Money {
public:
  /// Defaults to zero (no money).
  constexpr Money() = default;
  /// Builds an amount from a count of base units.
  explicit constexpr Money(int64_t baseUnits) : m_baseUnits(baseUnits) {}
  /// The amount in base units.
  [[nodiscard]] constexpr int64_t baseUnits() const noexcept {
    return m_baseUnits;
  }
  /// Whether the amount is negative (a debt).
  [[nodiscard]] constexpr bool isNegative() const noexcept {
    return m_baseUnits < 0;
  }

  [[nodiscard]] constexpr bool operator==(const Money &) const noexcept = default;
  [[nodiscard]] constexpr bool operator<(const Money &o) const noexcept {
    return m_baseUnits < o.m_baseUnits;
  }
  [[nodiscard]] constexpr bool operator<=(const Money &o) const noexcept {
    return m_baseUnits <= o.m_baseUnits;
  }
  [[nodiscard]] constexpr bool operator>(const Money &o) const noexcept {
    return o < *this;
  }
  [[nodiscard]] constexpr bool operator>=(const Money &o) const noexcept {
    return o <= *this;
  }
  [[nodiscard]] constexpr Money operator+(const Money &o) const noexcept {
    return Money{m_baseUnits + o.m_baseUnits};
  }
  [[nodiscard]] constexpr Money operator-(const Money &o) const noexcept {
    return Money{m_baseUnits - o.m_baseUnits};
  }
  constexpr Money &operator+=(const Money &o) noexcept {
    m_baseUnits += o.m_baseUnits;
    return *this;
  }
  constexpr Money &operator-=(const Money &o) noexcept {
    m_baseUnits -= o.m_baseUnits;
    return *this;
  }
  [[nodiscard]] constexpr Money operator*(int64_t factor) const noexcept {
    return Money{m_baseUnits * factor};
  }

  /// Builds an amount from a bag of coins, e.g.
  /// `Money::fromCoins(system, {{"gp", 2}, {"sp", 5}})`. Throws
  /// std::invalid_argument on an unknown denomination.
  [[nodiscard]] static Money
  fromCoins(const CurrencySystem &system,
            std::initializer_list<std::pair<std::string, int64_t>> coins) {
    int64_t total = 0;
    for (const auto &[denom, qty] : coins) {
      if (system.find(denom) == nullptr) {
        throw std::invalid_argument("unknown denomination '" + denom + "'");
      }
      total += system.valueOf(denom, qty);
    }
    return Money{total};
  }

  /// How many whole `denomId` coins this amount equals (floor division).
  /// 0 when the denomination is unknown or the amount is negative.
  [[nodiscard]] int64_t in(const CurrencySystem &system, std::string_view denomId) const {
    if (m_baseUnits <= 0) {
      return 0;
    }
    const Denomination *d = system.find(denomId);
    return d == nullptr ? 0 : m_baseUnits / d->perBase;
  }

private:
  int64_t m_baseUnits{0};
};

} // namespace rpg_os

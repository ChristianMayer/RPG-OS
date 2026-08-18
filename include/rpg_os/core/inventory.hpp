// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file inventory.hpp
 * @ingroup rpg_os_core
 * @brief Carried items: weight and a flat inventory.
 */
#pragma once

#include <cstdint>
#include <cstdlib>
#include <rpg_os/common/json.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg_os {

/// Trims leading and trailing whitespace from a string.
inline std::string trimWhitespace(std::string_view text) {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r' ||
                                 text[begin] == '\n')) {
    ++begin;
  }
  std::size_t end = text.size();
  while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r' ||
                         text[end - 1] == '\n')) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

/// Parses a human weight like "3", "1/2", "1 1/2", or "1.5" into a double.
/// Returns 0.0 for empty / unparsable input.
inline double parseFractionString(std::string_view text) {
  std::string trimmed = trimWhitespace(text);
  if (trimmed.empty()) {
    return 0.0;
  }
  const std::size_t space = trimmed.find(' ');
  std::string whole;
  std::string frac;
  if (space != std::string::npos) {
    whole = trimWhitespace(trimmed.substr(0, space));
    frac = trimWhitespace(trimmed.substr(space + 1));
  } else {
    frac = trimmed;
  }
  double result = whole.empty() ? 0.0 : std::strtod(whole.c_str(), nullptr);
  const std::size_t slash = frac.find('/');
  if (slash != std::string::npos) {
    const double numerator = std::strtod(frac.substr(0, slash).c_str(), nullptr);
    const double denominator = std::strtod(frac.substr(slash + 1).c_str(), nullptr);
    if (denominator != 0.0) {
      result += numerator / denominator;
    }
  } else if (!frac.empty()) {
    result += std::strtod(frac.c_str(), nullptr);
  }
  return result;
}

/// Parses the leading numeric portion of an item's weight field ("2 lb.",
/// "1/2 lb.", "8 lb.", "1 1/2 lb.") into a plain number. Returns -1.0 as a
/// sentinel when there is no numeric weight (e.g. "—", "Varies", "Slow").
inline double parseWeightValue(std::string_view text) {
  std::string trimmed = trimWhitespace(text);
  if (trimmed.empty() || trimmed == "-" || trimmed == "—" || trimmed == "Varies" ||
      trimmed == "varies" || trimmed == "Slow" || trimmed == "Push") {
    return -1.0;
  }
  std::size_t pos = 0;
  while (pos < trimmed.size() &&
         ((trimmed[pos] >= '0' && trimmed[pos] <= '9') || trimmed[pos] == '.' ||
          trimmed[pos] == '/' || trimmed[pos] == ' ' || trimmed[pos] == ',')) {
    ++pos;
  }
  if (pos == 0) {
    return -1.0;
  }
  return parseFractionString(trimmed.substr(0, pos));
}

/// A weight in some unit. Fractions are expected (D&D lists "1/2 lb."), so the
/// value is stored as a double; the unit is the ruleset's declared
/// `encumbrance.weight_unit` and weights are only ever compared within one
/// ruleset, so unit conversion never happens.
struct Weight {
  double value{0.0};
  std::string unit;

  [[nodiscard]] Weight operator+(const Weight &o) const {
    return Weight{value + o.value, unit};
  }
  [[nodiscard]] Weight operator-(const Weight &o) const {
    return Weight{value - o.value, unit};
  }
  Weight &operator+=(const Weight &o) {
    value += o.value;
    return *this;
  }
  [[nodiscard]] bool operator<(const Weight &o) const noexcept {
    return value < o.value;
  }
  [[nodiscard]] bool operator<=(const Weight &o) const noexcept {
    return value <= o.value;
  }
  [[nodiscard]] bool operator>(const Weight &o) const noexcept {
    return value > o.value;
  }
};

/// One stack of an item in a sheet's possession.
///
/// @par Why a stack, not a per-unit entry?
/// Inventory in practice is a list of "what and how many", not N individual
/// entries. A quantity field keeps the list short and the common operations
/// (add / remove / count) linear and simple.
///
/// @par Containers (bag-in-bags)
/// A stack may also hold @ref contents — items carried *inside* it (a
/// backpack holding a bedroll and a ration). Contents may themselves contain
/// items, so a stack is a tree rather than a flat leaf; the tree is walked
/// recursively for totals (@ref Inventory::totalCount) and weight sums
/// (@ref Inventory::visitStacks).
struct ItemInstance {
  std::string itemId; ///< id of the item record in `data.items`
  int32_t quantity{1};
  /// Items stored inside this stack (meaningful for quantity-1 containers).
  std::vector<ItemInstance> contents;
};

/// An inventory of owned item stacks, which may nest (containers).
class Inventory {
public:
  /// Adds `item` to the *flat* inventory, merging with an existing stack of
  /// the same id. Always succeeds (the flat model has no capacity limit).
  void add(const ItemInstance &item) {
    for (ItemInstance &entry : m_items) {
      if (entry.itemId == item.itemId) {
        entry.quantity += item.quantity;
        return;
      }
    }
    m_items.push_back(item);
  }

  /// Removes up to `quantity` of `itemId` from the *flat* inventory; returns
  /// false when the sheet does not carry enough (nothing is removed). Items
  /// inside a container are removed via @ref removeFrom instead.
  bool remove(std::string_view itemId, int32_t quantity = 1) {
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
      if (it->itemId == itemId) {
        if (it->quantity < quantity) {
          return false;
        }
        it->quantity -= quantity;
        if (it->quantity <= 0) {
          m_items.erase(it);
        }
        return true;
      }
    }
    return false;
  }

  /// Puts `item` inside the container `containerItemId` (a quantity-1 flat
  /// stack). Returns false when the container is absent or not a single unit.
  bool putInto(std::string_view containerItemId, const ItemInstance &item) {
    for (ItemInstance &entry : m_items) {
      if (entry.itemId == containerItemId && entry.quantity == 1) {
        entry.contents.push_back(item);
        return true;
      }
    }
    return false;
  }

  /// Removes up to `quantity` of `itemId` from inside `containerItemId`.
  /// Returns false when the container is absent or does not hold enough.
  bool removeFrom(std::string_view containerItemId, std::string_view itemId, int32_t quantity = 1) {
    for (ItemInstance &entry : m_items) {
      if (entry.itemId != containerItemId || entry.quantity != 1) {
        continue;
      }
      for (auto it = entry.contents.begin(); it != entry.contents.end(); ++it) {
        if (it->itemId == itemId) {
          if (it->quantity < quantity) {
            return false;
          }
          it->quantity -= quantity;
          if (it->quantity <= 0) {
            entry.contents.erase(it);
          }
          return true;
        }
      }
    }
    return false;
  }

  /// The contents of container `containerItemId` (nullptr when absent).
  [[nodiscard]] const std::vector<ItemInstance> *
  contentsOf(std::string_view containerItemId) const {
    for (const ItemInstance &entry : m_items) {
      if (entry.itemId == containerItemId && entry.quantity == 1) {
        return &entry.contents;
      }
    }
    return nullptr;
  }

  /// How many of `itemId` are inside `containerItemId` (0 when absent).
  [[nodiscard]] int32_t countIn(std::string_view containerItemId, std::string_view itemId) const {
    const std::vector<ItemInstance> *contents = contentsOf(containerItemId);
    if (contents == nullptr) {
      return 0;
    }
    int32_t total = 0;
    for (const ItemInstance &entry : *contents) {
      if (entry.itemId == itemId) {
        total += entry.quantity;
      }
    }
    return total;
  }

  /// How many of `itemId` are carried *flat* (0 when none). Use @ref totalCount
  /// to include items inside containers.
  [[nodiscard]] int32_t count(std::string_view itemId) const {
    for (const ItemInstance &entry : m_items) {
      if (entry.itemId == itemId) {
        return entry.quantity;
      }
    }
    return 0;
  }

  /// Whether at least one of `itemId` is carried flat.
  [[nodiscard]] bool has(std::string_view itemId) const {
    return count(itemId) > 0;
  }

  /// How many of `itemId` are carried anywhere — flat plus every container,
  /// recursively through nested containers.
  [[nodiscard]] int32_t totalCount(std::string_view itemId) const {
    int32_t total = 0;
    for (const ItemInstance &entry : m_items) {
      total += countInStack(entry, itemId);
    }
    return total;
  }

  /// Calls `visit` for every carried stack, depth-first: a flat stack first,
  /// then the stacks inside it, and so on. Weight sums and item listings use
  /// this to see everything a sheet carries (a container's own weight is
  /// visited alongside the weight of what is inside it).
  template <typename F> void visitStacks(F &&visit) const {
    for (const ItemInstance &entry : m_items) {
      visitStack(entry, visit);
    }
  }

  /// All flat stacks (immutable view).
  [[nodiscard]] const std::vector<ItemInstance> &items() const noexcept {
    return m_items;
  }

  /// Number of distinct flat stacks.
  [[nodiscard]] std::size_t size() const noexcept {
    return m_items.size();
  }

  void clear() noexcept {
    m_items.clear();
  }

  /// Serializes to a JSON array of {id, quantity, contents?} (contents nested).
  void toJson(Json &out) const {
    out = Json::array();
    for (const ItemInstance &entry : m_items) {
      out.push_back(itemToJson(entry));
    }
  }

  /// Loads from the array form produced by @ref toJson.
  void fromJson(const Json &in) {
    m_items.clear();
    if (!in.is_array()) {
      return;
    }
    for (const Json &entry : in) {
      ItemInstance item = itemFromJson(entry);
      if (!item.itemId.empty()) {
        m_items.push_back(std::move(item));
      }
    }
  }

private:
  /// Recursively counts `itemId` inside `stack` (itself plus its contents).
  static int32_t countInStack(const ItemInstance &stack, std::string_view itemId) {
    int32_t total = stack.itemId == itemId ? stack.quantity : 0;
    for (const ItemInstance &content : stack.contents) {
      total += countInStack(content, itemId);
    }
    return total;
  }

  /// Depth-first visit of `stack` (itself, then its contents).
  template <typename F> static void visitStack(const ItemInstance &stack, F &visit) {
    visit(stack);
    for (const ItemInstance &content : stack.contents) {
      visitStack(content, visit);
    }
  }

  /// Recursive item serialization (includes nested contents).
  static Json itemToJson(const ItemInstance &item) {
    Json out = {{"id", item.itemId}, {"quantity", item.quantity}};
    if (!item.contents.empty()) {
      Json contents = Json::array();
      for (const ItemInstance &content : item.contents) {
        contents.push_back(itemToJson(content));
      }
      out["contents"] = contents;
    }
    return out;
  }

  /// Recursive item deserialization (restores nested contents).
  static ItemInstance itemFromJson(const Json &in) {
    ItemInstance item;
    item.itemId = in.value("id", "");
    item.quantity = in.value("quantity", 1);
    if (in.contains("contents") && in.at("contents").is_array()) {
      for (const Json &content : in.at("contents")) {
        item.contents.push_back(itemFromJson(content));
      }
    }
    return item;
  }

  std::vector<ItemInstance> m_items;
};

} // namespace rpg_os

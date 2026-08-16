// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file combat_session.hpp
 * @brief Turn & action-economy orchestrator (universal mode).
 *
 * The code review's gap was "no turn & action-economy orchestrator": the
 * engine could tick one sheet's durations (@c RulesetEngine::runTurn) but
 * there was no object that owns an *order* of combatants, tracks how many
 * standard / bonus / reaction actions each has left this turn, and drives the
 * per-combatant turn so round-based effects (poison lasting 3 rounds) decay
 * automatically. This header closes that gap with @c CombatSession:
 *
 *   1. an initiative queue (rolled from each combatant's @c Initiative stat
 *      plus a die, re-rolled each round),
 *   2. a per-turn @c ActionBudget (D&D-like 1 standard / 1 bonus /
 *      1 reaction / movement points by default) enforced together with the
 *      existing @c RulesetEngine::actionAllowed restriction checks
 *      (a paralyzed combatant cannot spend an action), and
 *   3. @c CombatSession::runCurrentTurn, which calls the engine's
 *      @c RulesetEngine::runTurn for the acting combatant — firing
 *      @c OnTurnStart / @c OnTurnEnd, ticking durations, and firing
 *      recurring (ongoing) effects — so "poison deals 1d6 for 3 rounds"
 *      decays to nothing across three of that combatant's turns.
 *
 * @par Why a separate orchestrator instead of changing the fight simulator?
 * @c combat.hpp's @c runFight is a self-contained Monte-Carlo simulator used
 * by the ELO ranking; it intentionally consumes a scripted, deterministic RNG
 * stream and must not gain extra dice (ticking, initiative re-rolls would
 * perturb the stream and break thousands of scripted assertions). A
 * @c CombatSession is the interactive counterpart: it owns initiative order
 * and action economy for an application-driven combat where the caller
 * decides what each combatant does, and the engine handles the bookkeeping.
 *
 * @par Who owns the entities?
 * The session does not own entities — it stores @c EntityHandle s into a
 * @c EntityRegistry (or @c GameSession). Removing an entity from the
 * registry makes its handle expire; the session skips expired combatants, so
 * a creature that dies mid-round simply stops taking turns.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <rpg_os/core/entity.hpp>
#include <rpg_os/universal/dynamic_entity.hpp>
#include <rpg_os/universal/engine.hpp>
#include <rpg_os/universal/entity_registry.hpp>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg_os {

/// Which kind of action a combatant is spending (or being refused).
///
/// @par Why an enum instead of a string?
/// The engine's restriction vocabulary is string-based
/// (@c RulesetEngine::actionAllowed takes "action", "bonus_action",
/// "reaction", "move"), but the budget bookkeeping needs typed, switchable
/// cases. The enum keeps the action names in one place and maps to the engine
/// strings via @ref actionName.
enum class ActionKind : std::uint8_t {
  Action,      ///< the standard action (engine string "action")
  BonusAction, ///< the bonus action ("bonus_action")
  Reaction,    ///< the reaction ("reaction")
  Move,        ///< movement points ("move")
};

/// The engine-string form of `kind` (for @c RulesetEngine::actionAllowed).
[[nodiscard]] inline std::string_view actionName(ActionKind kind) noexcept {
  switch (kind) {
  case ActionKind::Action:
    return "action";
  case ActionKind::BonusAction:
    return "bonus_action";
  case ActionKind::Reaction:
    return "reaction";
  case ActionKind::Move:
    return "move";
  }
  return "action";
}

/// Per-turn action budget for a combatant.
///
/// @par Why a plain struct with counts?
/// Different rulesets budget turns differently (D&D: 1 action + 1 bonus +
/// 1 reaction; others may allow more or fewer, or track movement in points).
/// A small struct with sensible D&D-like defaults lets an encounter supply
/// its own numbers while staying trivial to construct. Movement is a point
/// pool (e.g. a D&D speed of 30 feet = 30 points) spent by the foot, so a
/// "move up to your speed" rule is a single @c spendAction(Move, n).
struct ActionBudget {
  int32_t actions{1};      ///< standard actions per turn
  int32_t bonusActions{1}; ///< bonus actions per turn
  int32_t reactions{1};    ///< reactions per turn
  int32_t movement{0};     ///< movement points per turn (0 = no tracked movement)
};

/// One participant in a @ref CombatSession.
///
/// A combatant wraps an @ref EntityHandle (so it can die and be skipped), a
/// 0-based side (for the caller's own ally/enemy grouping), its rolled
/// initiative, and how much of this turn's budget it has already spent.
struct Combatant {
  EntityHandle handle; ///< the live entity; expired means "dead / removed"
  int32_t side{0};     ///< caller-defined group (0 and 1 for the classic two sides)
  int32_t initiative{0};
  int32_t spentActions{0};
  int32_t spentBonusActions{0};
  int32_t spentReactions{0};
  int32_t spentMovement{0};
};

/// Owns an initiative order and per-turn action economy for one combat.
///
/// @par Typical use
/// \code
/// CombatSession combat(engine);
/// combat.addCombatant(session.createCharacterHandle("geron"), 0);
/// combat.addCombatant(session.createCreatureHandle("goblin"), 1);
/// combat.beginRound(rng);
/// while (combat.next()) {
///   combat.runCurrentTurn(rng);          // tick durations, fire turn events
///   auto who = combat.current();
///   combat.spendAction(who.entityId(), ActionKind::Action); // the caller's action
/// }
/// \endcode
class CombatSession {
public:
  /// Creates a session over `engine` with the given per-turn @ref ActionBudget.
  explicit CombatSession(RulesetEngine &engine, ActionBudget budget = {})
      : m_engine(&engine), m_budget(budget) {}

  /// Adds a combatant (a live entity handle) on `side`; returns its id.
  [[nodiscard]] EntityId addCombatant(EntityHandle handle, int32_t side = 0) {
    m_combatants.push_back(Combatant{std::move(handle), side, 0, 0, 0, 0, 0});
    return m_combatants.back().handle.entityId();
  }

  /// Rolls initiative for every combatant and sorts them descending (highest
  /// first). Each combatant rolls its @c Initiative stat plus @c 1d6 from
  /// the caller's RNG; ties keep registration order (stable). Expired
  /// combatants keep initiative 0 and sort last.
  template <RandomNumberGenerator Rng>
  void rollInitiative(Rng &rng, std::string_view statId = "Initiative") {
    for (Combatant &combatant : m_combatants) {
      DynamicEntity *entity = combatant.handle.resolve();
      combatant.initiative = entity != nullptr ? entity->getStat(statId) + rng(1, 6) : 0;
    }
    std::stable_sort(
        m_combatants.begin(), m_combatants.end(),
        [](const Combatant &a, const Combatant &b) { return a.initiative > b.initiative; });
  }

  /// Starts a new round: increments the round counter, re-rolls initiative,
  /// and rewinds the turn pointer so @ref next can walk the order again.
  /// The per-combatant budget is reset when that combatant's turn runs (see
  /// @ref runCurrentTurn).
  template <RandomNumberGenerator Rng>
  void beginRound(Rng &rng, std::string_view statId = "Initiative") {
    ++m_round;
    rollInitiative(rng, statId);
    m_nextIndex = 0;
    m_currentIndex = m_combatants.size();
  }

  /// Advances to the next living combatant; returns false when the round is
  /// over (every combatant has acted, or none remain).
  bool next() {
    while (m_nextIndex < m_combatants.size()) {
      const std::size_t index = m_nextIndex++;
      if (m_combatants[index].handle.resolve() != nullptr) {
        m_currentIndex = index;
        return true;
      }
    }
    m_currentIndex = m_combatants.size();
    return false;
  }

  /// The combatant whose turn it is (a null handle when the round is over).
  [[nodiscard]] EntityHandle current() const {
    return m_currentIndex < m_combatants.size() ? m_combatants[m_currentIndex].handle
                                                : EntityHandle{};
  }

  /// Runs the current combatant's turn: resets its budget, then calls
  /// @c RulesetEngine::runTurn — firing @c OnTurnStart / @c OnTurnEnd, ticking
  /// effect durations, and firing recurring effects — so round-based effects
  /// decay across turns. Returns the acting combatant's handle (null when
  /// there is no current combatant).
  template <RandomNumberGenerator Rng> EntityHandle runCurrentTurn(Rng &rng) {
    if (m_currentIndex >= m_combatants.size()) {
      return EntityHandle{};
    }
    Combatant &combatant = m_combatants[m_currentIndex];
    DynamicEntity *entity = combatant.handle.resolve();
    if (entity == nullptr) {
      return EntityHandle{};
    }
    resetBudget(combatant);
    m_engine->runTurn(*entity, rng);
    return combatant.handle;
  }

  /// Whether `id` may spend `amount` of `kind` this turn: the combatant must
  /// be alive, not restricted by a condition (via
  /// @c RulesetEngine::actionAllowed), and have budget remaining.
  [[nodiscard]] bool canSpend(EntityId id, ActionKind kind, int32_t amount = 1) const {
    const Combatant *combatant = findCombatant(id);
    if (combatant == nullptr) {
      return false;
    }
    const DynamicEntity *entity = combatant->handle.resolve();
    if (entity == nullptr || amount < 0) {
      return false;
    }
    if (!m_engine->actionAllowed(*entity, actionName(kind))) {
      return false;
    }
    return availableFor(*combatant, kind) >= amount;
  }

  /// Spends `amount` of `kind` for the combatant `id`; returns false (and
  /// spends nothing) when the combatant is absent, dead, restricted, or out
  /// of budget.
  bool spendAction(EntityId id, ActionKind kind, int32_t amount = 1) {
    if (!canSpend(id, kind, amount)) {
      return false;
    }
    Combatant *combatant = findCombatant(id);
    spentFor(*combatant, kind) += amount;
    return true;
  }

  /// How much of `kind` the combatant `id` can still spend this turn.
  [[nodiscard]] int32_t remaining(EntityId id, ActionKind kind) const {
    const Combatant *combatant = findCombatant(id);
    return combatant == nullptr ? 0 : availableFor(*combatant, kind);
  }

  /// The current round number (0 before the first @ref beginRound).
  [[nodiscard]] int32_t round() const noexcept {
    return m_round;
  }

  /// Whether every combatant has acted this round (the next @ref next call
  /// would return false) or the combat is empty.
  [[nodiscard]] bool ended() const noexcept {
    return m_nextIndex >= m_combatants.size();
  }

  /// Number of combatants registered.
  [[nodiscard]] std::size_t combatantCount() const noexcept {
    return m_combatants.size();
  }

  /// All combatants, in current initiative order (immutable view).
  [[nodiscard]] const std::vector<Combatant> &combatants() const noexcept {
    return m_combatants;
  }

private:
  [[nodiscard]] static int32_t budgetTotal(const ActionBudget &budget, ActionKind kind) noexcept {
    switch (kind) {
    case ActionKind::Action:
      return budget.actions;
    case ActionKind::BonusAction:
      return budget.bonusActions;
    case ActionKind::Reaction:
      return budget.reactions;
    case ActionKind::Move:
      return budget.movement;
    }
    return 0;
  }

  [[nodiscard]] static int32_t &spentFor(Combatant &combatant, ActionKind kind) noexcept {
    switch (kind) {
    case ActionKind::Action:
      return combatant.spentActions;
    case ActionKind::BonusAction:
      return combatant.spentBonusActions;
    case ActionKind::Reaction:
      return combatant.spentReactions;
    case ActionKind::Move:
      return combatant.spentMovement;
    }
    return combatant.spentActions;
  }

  /// Budget remaining for `kind` on `combatant` (total minus spent).
  [[nodiscard]] int32_t availableFor(const Combatant &combatant, ActionKind kind) const noexcept {
    const int32_t total = budgetTotal(m_budget, kind);
    switch (kind) {
    case ActionKind::Action:
      return total - combatant.spentActions;
    case ActionKind::BonusAction:
      return total - combatant.spentBonusActions;
    case ActionKind::Reaction:
      return total - combatant.spentReactions;
    case ActionKind::Move:
      return total - combatant.spentMovement;
    }
    return 0;
  }

  static void resetBudget(Combatant &combatant) noexcept {
    combatant.spentActions = 0;
    combatant.spentBonusActions = 0;
    combatant.spentReactions = 0;
    combatant.spentMovement = 0;
  }

  [[nodiscard]] Combatant *findCombatant(EntityId id) {
    for (Combatant &combatant : m_combatants) {
      if (combatant.handle.entityId() == id) {
        return &combatant;
      }
    }
    return nullptr;
  }
  [[nodiscard]] const Combatant *findCombatant(EntityId id) const {
    for (const Combatant &combatant : m_combatants) {
      if (combatant.handle.entityId() == id) {
        return &combatant;
      }
    }
    return nullptr;
  }

  RulesetEngine *m_engine;
  ActionBudget m_budget;
  std::vector<Combatant> m_combatants;
  std::size_t m_nextIndex{0};
  std::size_t m_currentIndex{0};
  int32_t m_round{0};
};

} // namespace rpg_os

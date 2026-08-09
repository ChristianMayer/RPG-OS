// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_event_system.cpp
 * @brief Tests for the shared event system (@c rpg_os::EventBus,
 * @c rpg_os::EventData).
 *
 * Verifies listener registration/removal by handle, dispatch order, the typed
 * payload getters and their fallback behaviour. Because the event system is
 * the seam between the JSON-driven rules and user code, these tests pin the
 * ordering contract that damage/turn pipelines rely on.
 */
#include <doctest/doctest.h>
#include <rpg_os/common/event_system.hpp>
#include <string>

TEST_CASE("EventBus: listener is invoked on dispatch") {
  rpg_os::EventBus bus;
  int invoked = 0;
  const uint64_t id = bus.addListener(rpg_os::EventType::OnDamageTaken,
                                      [&](const rpg_os::EventData &) { ++invoked; });
  CHECK(id != 0);
  CHECK(bus.listenerCount(rpg_os::EventType::OnDamageTaken) == 1);
  bus.dispatch(rpg_os::EventType::OnDamageTaken, {});
  CHECK(invoked == 1);
}

TEST_CASE("EventBus: listeners only fire for their own type") {
  rpg_os::EventBus bus;
  int a = 0;
  int b = 0;
  const uint64_t idA =
      bus.addListener(rpg_os::EventType::OnDamageTaken, [&](const rpg_os::EventData &) { ++a; });
  const uint64_t idB =
      bus.addListener(rpg_os::EventType::OnTurnStart, [&](const rpg_os::EventData &) { ++b; });
  bus.dispatch(rpg_os::EventType::OnTurnStart, {});
  CHECK(a == 0);
  CHECK(b == 1);
  bus.dispatch(rpg_os::EventType::OnDamageTaken, {});
  CHECK(a == 1);
  CHECK(b == 1);
  CHECK(bus.removeListener(idA));
  CHECK(bus.removeListener(idB));
}

TEST_CASE("EventBus: listeners run in registration order") {
  rpg_os::EventBus bus;
  std::string order;
  const uint64_t id1 = bus.addListener(rpg_os::EventType::OnTurnEnd,
                                       [&](const rpg_os::EventData &) { order += "1"; });
  const uint64_t id2 = bus.addListener(rpg_os::EventType::OnTurnEnd,
                                       [&](const rpg_os::EventData &) { order += "2"; });
  CHECK(id2 > id1); // handles are unique and monotonically increasing
  bus.dispatch(rpg_os::EventType::OnTurnEnd, {});
  CHECK(order == "12");
}

TEST_CASE("EventBus: removed listener is not invoked") {
  rpg_os::EventBus bus;
  int invoked = 0;
  const uint64_t id = bus.addListener(rpg_os::EventType::OnDamageCalculated,
                                      [&](const rpg_os::EventData &) { ++invoked; });
  CHECK(bus.removeListener(id));
  CHECK_FALSE(bus.removeListener(id));
  bus.dispatch(rpg_os::EventType::OnDamageCalculated, {});
  CHECK(invoked == 0);
}

TEST_CASE("EventBus: dispatch with no listeners is a no-op") {
  rpg_os::EventBus bus;
  CHECK_NOTHROW(bus.dispatch(rpg_os::EventType::OnTurnEnd, {}));
}

TEST_CASE("EventData: typed getters with defaults") {
  rpg_os::EventData data;
  data.payload = {{"damage", 12}, {"attacker_visible", true}, {"note", "hi"}};
  CHECK(data.getInt("damage") == 12);
  CHECK(data.getInt("missing") == 0);
  CHECK(data.getInt("missing", 7) == 7);
  CHECK(data.getBool("attacker_visible") == true);
  CHECK(data.getBool("missing") == false);
  CHECK(data.getString("note") == "hi");
  CHECK(data.getString("missing", "x") == "x");
}

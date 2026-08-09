// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_expression.cpp
 * @brief Tests for the universal AST formula evaluator (@c rpg_os::Expression).
 *
 * Covers parsing (precedence, associativity, parens), evaluation against a
 * scripted context, the full function set, and the error paths (unresolved
 * identifiers, division by zero, malformed input). These tests pin the
 * restricted grammar that the code generator must also translate to C++.
 */
#include <doctest/doctest.h>
#include <rpg_os/universal/expression.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

// EvalContext backed by a simple path -> value map.
struct MapEvalContext : rpg_os::EvalContext {
  std::unordered_map<std::string, double> values;

  bool resolve(std::string_view path, double &out) const override {
    const auto it = values.find(std::string(path));
    if (it == values.end()) {
      return false;
    }
    out = it->second;
    return true;
  }
};

double eval(std::string_view text, const MapEvalContext &context = {}) {
  return rpg_os::Expression(text).evaluate(context);
}

} // namespace

TEST_CASE("Expression: arithmetic precedence") {
  CHECK(eval("2 + 3 * 4") == doctest::Approx(14.0));
  CHECK(eval("(2 + 3) * 4") == doctest::Approx(20.0));
  CHECK(eval("10 - 4 - 3") == doctest::Approx(3.0)); // left associative
  CHECK(eval("20 / 4 / 2") == doctest::Approx(2.5)); // left associative
  CHECK(eval("10 / 4") == doctest::Approx(2.5));
}

TEST_CASE("Expression: modulo and power") {
  CHECK(eval("10 % 3") == doctest::Approx(1.0));
  CHECK(eval("2 ^ 10") == doctest::Approx(1024.0));
  CHECK(eval("2 ^ 3 ^ 2") == doctest::Approx(512.0)); // right associative
  CHECK(eval("-2 ^ 2") == doctest::Approx(-4.0));     // -(2^2)
}

TEST_CASE("Expression: unary operators") {
  CHECK(eval("-5 + 3") == doctest::Approx(-2.0));
  CHECK(eval("!0") == doctest::Approx(1.0));
  CHECK(eval("!5") == doctest::Approx(0.0));
  CHECK(eval("!!5") == doctest::Approx(1.0));
  CHECK(eval("-(3 + 4)") == doctest::Approx(-7.0));
}

TEST_CASE("Expression: comparisons") {
  CHECK(eval("5 > 3") == doctest::Approx(1.0));
  CHECK(eval("5 <= 3") == doctest::Approx(0.0));
  CHECK(eval("1 == 1") == doctest::Approx(1.0));
  CHECK(eval("1 != 2") == doctest::Approx(1.0));
  CHECK(eval("2 <= 2") == doctest::Approx(1.0));
  CHECK(eval("3 >= 4") == doctest::Approx(0.0));
}

TEST_CASE("Expression: logical operators") {
  CHECK(eval("1 && 0") == doctest::Approx(0.0));
  CHECK(eval("1 && 1") == doctest::Approx(1.0));
  CHECK(eval("1 || 0") == doctest::Approx(1.0));
  CHECK(eval("0 || 0") == doctest::Approx(0.0));
  CHECK(eval("(5 > 3) && (2 < 4)") == doctest::Approx(1.0));
}

TEST_CASE("Expression: built-in functions") {
  CHECK(eval("min(3, 7)") == doctest::Approx(3.0));
  CHECK(eval("max(3, 7)") == doctest::Approx(7.0));
  CHECK(eval("floor(7.5)") == doctest::Approx(7.0));
  CHECK(eval("ceil(7.2)") == doctest::Approx(8.0));
  CHECK(eval("round(2.5)") == doctest::Approx(3.0));
  CHECK(eval("clamp(5, 0, 3)") == doctest::Approx(3.0));
  CHECK(eval("min(max(3, 7), 5)") == doctest::Approx(5.0));
}

TEST_CASE("Expression: resolves identifiers through the context") {
  MapEvalContext context;
  context.values = {{"COU", 12.0}, {"AGI", 13.0}, {"STR", 11.0}};
  CHECK(eval("COU + AGI + STR", context) == doctest::Approx(36.0));
  // An unknown path cannot be resolved -> runtime error.
  CHECK_THROWS_AS(eval("actor.ST", context), std::runtime_error);
}

TEST_CASE("Expression: DSA base attack formula") {
  MapEvalContext context;
  context.values = {{"COU", 12.0}, {"AGI", 13.0}, {"STR", 11.0}};
  // floor((COU + AGI + STR) / 5) = floor(36 / 5) = floor(7.2) = 7
  CHECK(eval("floor((COU + AGI + STR) / 5)", context) == doctest::Approx(7.0));
  // round((CN + CN + ST) / 2) with CN=12, ST=11 -> round(35/2)=round(17.5)=18
  MapEvalContext vp;
  vp.values = {{"CN", 12.0}, {"ST", 11.0}};
  CHECK(eval("round((CN + CN + ST) / 2)", vp) == doctest::Approx(18.0));
}

TEST_CASE("Expression: dotted context paths") {
  MapEvalContext context;
  context.values = {{"actor.ST", 14.0}, {"target.AC", 15.0}, {"env.light_level", 1.0}};
  CHECK(eval("actor.ST", context) == doctest::Approx(14.0));
  CHECK(eval("target.AC", context) == doctest::Approx(15.0));
  CHECK(eval("env.light_level > 0", context) == doctest::Approx(1.0));
}

TEST_CASE("Expression: identifiers() collects unique references in order") {
  const rpg_os::Expression expr("floor((COU + AGI + STR) / 5) + COU");
  const std::vector<std::string> ids = expr.identifiers();
  REQUIRE(ids.size() == 3);
  CHECK(ids[0] == "COU");
  CHECK(ids[1] == "AGI");
  CHECK(ids[2] == "STR");
}

TEST_CASE("Expression: malformed formulas throw") {
  CHECK_THROWS_AS(rpg_os::Expression("2 +"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::Expression(""), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::Expression("(1 + 2"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::Expression("1 + ) 2"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::Expression("3 + * 4"), std::invalid_argument);
  CHECK_THROWS_AS(rpg_os::Expression("1 2"), std::invalid_argument);
}

TEST_CASE("Expression: evaluation-time errors") {
  MapEvalContext context;
  // Unknown function -> runtime_error at evaluation.
  const rpg_os::Expression unknownFn("unknown_func(1, 2)");
  CHECK_THROWS_AS((void)unknownFn.evaluate(context), std::runtime_error);
  // Unresolved identifier -> runtime_error.
  const rpg_os::Expression unresolved("MISSING + 1");
  CHECK_THROWS_AS((void)unresolved.evaluate(context), std::runtime_error);
  // Division by zero -> runtime_error.
  const rpg_os::Expression divZero("10 / 0");
  CHECK_THROWS_AS((void)divZero.evaluate(context), std::runtime_error);
  // Wrong arity for min -> runtime_error.
  const rpg_os::Expression badArity("min(1)");
  CHECK_THROWS_AS((void)badArity.evaluate(context), std::runtime_error);
}

TEST_CASE("Expression: clamp with correct argument order") {
  CHECK(eval("clamp(-5, 0, 10)") == doctest::Approx(0.0));
  CHECK(eval("clamp(15, 0, 10)") == doctest::Approx(10.0));
  CHECK(eval("clamp(5, 0, 10)") == doctest::Approx(5.0));
}

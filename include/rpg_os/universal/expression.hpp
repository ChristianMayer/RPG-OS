// Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
// SPDX-License-Identifier: Apache-2.0

/**
 * @file expression.hpp
 * @ingroup rpg_os_universal
 * @brief AST formula evaluator (universal / dynamic mode).
 *
 * Parses the restricted formula grammar used by ruleset JSON derived stats,
 * check conditions, and event conditions, and evaluates it against an
 * @c EvalContext that resolves identifiers (bare ids, @c actor.*,
 * @c target.*, @c env.*, @c event.*, @c action.*).
 *
 * This is the *universal-only* runtime evaluator: specific mode compiles the
 * same grammar to C++ instead, so generated code never includes this header.
 *
 * @par Why a restricted grammar?
 * Derived-stat formulas must be translatable to C++ by the code generator
 * (see @c codegen/rpg_os_codegen.py). Restricting the grammar to a small,
 * well-defined set — arithmetic, comparisons, boolean operators, and the
 * @c min/@c max/@c floor/@c ceil/@c round/@c clamp functions — is what makes
 * that translation possible and safe. It also keeps rulesets predictable:
 * no loops, no side effects, no arbitrary function calls in a stat formula.
 *
 * Grammar (see @c rulesets/ruleset.schema.json for the ruleset format):
 *   numbers, identifiers (dotted allowed), + - * / % ^, comparisons, && || !,
 *   functions min max floor ceil round clamp, parentheses, commas.
 */
#pragma once

#include <cstddef>
#include <rpg_os/core/math.hpp>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rpg_os {

/**
 * Resolves a formula identifier to a value.
 *
 * @par Why an abstract resolver instead of a fixed callback?
 * The same evaluator serves several "worlds": derived stats (bare ids),
 * combat formulas (@c actor.* vs @c target.*), event conditions
 * (@c event.*), and environment-dependent formulas (@c env.*). Abstracting
 * the lookup behind @c EvalContext lets one evaluator work with any of them —
 * and lets the universal engine's @ref EntityContext (see
 * @ref dynamic_entity.hpp) provide all of them at once.
 */
class EvalContext {
public:
  virtual ~EvalContext() = default;

  /// Resolves `path` (e.g. "COU", "actor.ST", "target.AC", "env.light_level")
  /// into `out`. Returns false if the path cannot be resolved.
  ///
  /// @par Why return bool instead of a sentinel value?
  /// A stat value of 0 is a legitimate answer, so it cannot double as "not
  /// found". Returning success/failure lets the evaluator distinguish "the
  /// stat is genuinely 0" from "this identifier does not exist here" and
  /// report the latter as an error.
  [[nodiscard]] virtual bool resolve(std::string_view path, double &out) const = 0;
};

/**
 * A parsed formula. Immutable after construction.
 *
 * @par Why parse eagerly in the constructor?
 * Rulesets are validated at load time. Constructing the @c Expression (and
 * thus parsing) immediately means a malformed formula fails the load with a
 * precise error instead of surfacing halfway through a game session.
 */
class Expression {
public:
  /// Default-constructs the empty expression (evaluates to 0.0). Useful for
  /// optional condition fields that were not present in a ruleset.
  Expression() = default;

  /// Parses `text`; throws std::invalid_argument on a syntax error.
  explicit Expression(std::string_view text);

  /// Evaluates the formula; throws std::runtime_error on an unresolved
  /// identifier, division by zero, or an arity error.
  ///
  /// @par Why exceptions for evaluation errors?
  /// A formula that cannot be evaluated is a data bug, not a control-flow
  /// case; failing loudly at the point of evaluation keeps the caller from
  /// silently applying a wrong derived stat.
  [[nodiscard]] double evaluate(const EvalContext &context) const;

  /// Every identifier referenced by the formula (deduplicated, in order of
  /// first appearance). Used for ruleset validation.
  ///
  /// @par Why expose the identifier list?
  /// The loader uses it to check that every referenced stat actually exists
  /// and that derived-stat graphs are acyclic — both checks need the raw
  /// identifier set without evaluating the formula.
  [[nodiscard]] std::vector<std::string> identifiers() const;

  /// The original formula text.
  [[nodiscard]] const std::string &text() const noexcept {
    return m_text;
  }

private:
  struct Node {
    enum class Kind { Number, Identifier, Unary, Binary, Call };
    Kind kind{Kind::Number};
    double number{0.0};
    std::string identifier; ///< Identifier
    std::string op;         ///< Unary / Binary operator
    std::string function;   ///< Call function name
    std::vector<Node> children;
  };

  struct Parser {
    std::string_view text;
    std::size_t pos{0};

    explicit Parser(std::string_view t) : text(t) {}

    Node parse() {
      skipWs();
      Node node = parseOr();
      skipWs();
      if (pos != text.size()) {
        throw std::invalid_argument("unexpected trailing characters in formula '" +
                                    std::string(text) + "'");
      }
      return node;
    }

    void skipWs() {
      while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n')) {
        ++pos;
      }
    }

    bool consume(std::string_view token) {
      if (text.substr(pos, token.size()) == token) {
        pos += token.size();
        return true;
      }
      return false;
    }

    [[nodiscard]] bool isDigit(char c) const {
      return c >= '0' && c <= '9';
    }
    [[nodiscard]] bool isIdStart(char c) const {
      return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    }
    [[nodiscard]] bool isIdChar(char c) const {
      return isIdStart(c) || isDigit(c) || c == '.';
    }

    Node makeNumber(double value) {
      Node n;
      n.kind = Node::Kind::Number;
      n.number = value;
      return n;
    }

    Node makeIdentifier(std::string id) {
      Node n;
      n.kind = Node::Kind::Identifier;
      n.identifier = std::move(id);
      return n;
    }

    Node makeUnary(std::string op, Node child) {
      Node n;
      n.kind = Node::Kind::Unary;
      n.op = std::move(op);
      n.children.push_back(std::move(child));
      return n;
    }

    Node makeBinary(std::string op, Node left, Node right) {
      Node n;
      n.kind = Node::Kind::Binary;
      n.op = std::move(op);
      n.children.push_back(std::move(left));
      n.children.push_back(std::move(right));
      return n;
    }

    Node makeCall(std::string fn, std::vector<Node> args) {
      Node n;
      n.kind = Node::Kind::Call;
      n.function = std::move(fn);
      n.children = std::move(args);
      return n;
    }

    double parseNumber() {
      std::size_t start = pos;
      while (pos < text.size() && isDigit(text[pos])) {
        ++pos;
      }
      if (pos < text.size() && text[pos] == '.') {
        ++pos;
        while (pos < text.size() && isDigit(text[pos])) {
          ++pos;
        }
      }
      if (pos == start) {
        throw std::invalid_argument("expected a number in formula '" + std::string(text) + "'");
      }
      const double value = std::stod(std::string(text.substr(start, pos - start)));
      return value;
    }

    std::string parseIdentifier() {
      const std::size_t start = pos;
      while (pos < text.size() && isIdChar(text[pos])) {
        ++pos;
      }
      return std::string(text.substr(start, pos - start));
    }

    // ---- grammar (precedence climbing) ----

    Node parseOr() {
      Node left = parseAnd();
      while (true) {
        skipWs();
        if (consume("||")) {
          Node right = parseAnd();
          left = makeBinary("||", std::move(left), std::move(right));
        } else {
          return left;
        }
      }
    }

    Node parseAnd() {
      Node left = parseEquality();
      while (true) {
        skipWs();
        if (consume("&&")) {
          Node right = parseEquality();
          left = makeBinary("&&", std::move(left), std::move(right));
        } else {
          return left;
        }
      }
    }

    Node parseEquality() {
      Node left = parseRelational();
      while (true) {
        skipWs();
        std::string op;
        if (consume("==")) {
          op = "==";
        } else if (consume("!=")) {
          op = "!=";
        } else {
          return left;
        }
        Node right = parseRelational();
        left = makeBinary(std::move(op), std::move(left), std::move(right));
      }
    }

    Node parseRelational() {
      Node left = parseAdditive();
      while (true) {
        skipWs();
        std::string op;
        if (consume("<=")) {
          op = "<=";
        } else if (consume(">=")) {
          op = ">=";
        } else if (consume("<")) {
          op = "<";
        } else if (consume(">")) {
          op = ">";
        } else {
          return left;
        }
        Node right = parseAdditive();
        left = makeBinary(std::move(op), std::move(left), std::move(right));
      }
    }

    Node parseAdditive() {
      Node left = parseMultiplicative();
      while (true) {
        skipWs();
        std::string op;
        if (consume("+")) {
          op = "+";
        } else if (consume("-")) {
          op = "-";
        } else {
          return left;
        }
        Node right = parseMultiplicative();
        left = makeBinary(std::move(op), std::move(left), std::move(right));
      }
    }

    Node parseMultiplicative() {
      Node left = parseUnary();
      while (true) {
        skipWs();
        std::string op;
        if (consume("*")) {
          op = "*";
        } else if (consume("/")) {
          op = "/";
        } else if (consume("%")) {
          op = "%";
        } else {
          return left;
        }
        Node right = parseUnary();
        left = makeBinary(std::move(op), std::move(left), std::move(right));
      }
    }

    Node parseUnary() {
      skipWs();
      if (consume("!")) {
        return makeUnary("!", parseUnary());
      }
      if (consume("-")) {
        return makeUnary("-", parseUnary());
      }
      return parsePower();
    }

    Node parsePower() {
      Node left = parsePrimary();
      skipWs();
      if (consume("^")) {
        // Right-associative: 2 ^ 3 ^ 2 == 2 ^ (3 ^ 2).
        Node right = parseUnary();
        return makeBinary("^", std::move(left), std::move(right));
      }
      return left;
    }

    Node parsePrimary() {
      skipWs();
      if (pos >= text.size()) {
        throw std::invalid_argument("unexpected end of formula '" + std::string(text) + "'");
      }
      if (text[pos] == '(') {
        ++pos;
        Node node = parseOr();
        skipWs();
        if (!consume(")")) {
          throw std::invalid_argument("missing ')' in formula '" + std::string(text) + "'");
        }
        return node;
      }
      if (isDigit(text[pos]) || text[pos] == '.') {
        return makeNumber(parseNumber());
      }
      if (isIdStart(text[pos])) {
        const std::string id = parseIdentifier();
        skipWs();
        if (consume("(")) {
          std::vector<Node> args;
          skipWs();
          if (!consume(")")) {
            while (true) {
              args.push_back(parseOr());
              skipWs();
              if (consume(")")) {
                break;
              }
              if (!consume(",")) {
                throw std::invalid_argument("expected ',' or ')' in formula '" + std::string(text) +
                                            "'");
              }
              skipWs();
            }
          }
          return makeCall(id, std::move(args));
        }
        return makeIdentifier(id);
      }
      throw std::invalid_argument("unexpected character '" + std::string(1, text[pos]) +
                                  "' in formula '" + std::string(text) + "'");
    }
  };

  static double evaluateNode(const Node &node, const EvalContext &context);
  static void collectIdentifiers(const Node &node, std::vector<std::string> &out);

  std::string m_text;
  Node m_root;
};

inline Expression::Expression(std::string_view text) : m_text(text) {
  m_root = Parser(text).parse();
}

inline double Expression::evaluate(const EvalContext &context) const {
  return evaluateNode(m_root, context);
}

inline std::vector<std::string> Expression::identifiers() const {
  std::vector<std::string> out;
  collectIdentifiers(m_root, out);
  return out;
}

inline double Expression::evaluateNode(const Node &node, const EvalContext &context) {
  switch (node.kind) {
  case Node::Kind::Number:
    return node.number;
  case Node::Kind::Identifier: {
    double value = 0.0;
    if (!context.resolve(node.identifier, value)) {
      throw std::runtime_error("unresolved identifier '" + node.identifier + "'");
    }
    return value;
  }
  case Node::Kind::Unary: {
    const double operand = evaluateNode(node.children[0], context);
    if (node.op == "!") {
      return operand == 0.0 ? 1.0 : 0.0;
    }
    if (node.op == "-") {
      return -operand;
    }
    throw std::runtime_error("unknown unary operator '" + node.op + "'");
  }
  case Node::Kind::Binary: {
    const double lhs = evaluateNode(node.children[0], context);
    const double rhs = evaluateNode(node.children[1], context);
    const std::string &op = node.op;
    if (op == "+") {
      return lhs + rhs;
    }
    if (op == "-") {
      return lhs - rhs;
    }
    if (op == "*") {
      return lhs * rhs;
    }
    if (op == "/") {
      if (rhs == 0.0) {
        throw std::runtime_error("division by zero in formula");
      }
      return lhs / rhs;
    }
    if (op == "%") {
      if (rhs == 0.0) {
        throw std::runtime_error("modulo by zero in formula");
      }
      return std::fmod(lhs, rhs);
    }
    if (op == "^") {
      return std::pow(lhs, rhs);
    }
    if (op == "==") {
      return lhs == rhs ? 1.0 : 0.0;
    }
    if (op == "!=") {
      return lhs != rhs ? 1.0 : 0.0;
    }
    if (op == "<") {
      return lhs < rhs ? 1.0 : 0.0;
    }
    if (op == ">") {
      return lhs > rhs ? 1.0 : 0.0;
    }
    if (op == "<=") {
      return lhs <= rhs ? 1.0 : 0.0;
    }
    if (op == ">=") {
      return lhs >= rhs ? 1.0 : 0.0;
    }
    if (op == "&&") {
      return (lhs != 0.0 && rhs != 0.0) ? 1.0 : 0.0;
    }
    if (op == "||") {
      return (lhs != 0.0 || rhs != 0.0) ? 1.0 : 0.0;
    }
    throw std::runtime_error("unknown binary operator '" + op + "'");
  }
  case Node::Kind::Call: {
    std::vector<double> args;
    args.reserve(node.children.size());
    for (const Node &child : node.children) {
      args.push_back(evaluateNode(child, context));
    }
    const std::string &fn = node.function;
    if (fn == "min") {
      if (args.size() != 2) {
        throw std::runtime_error("min() expects 2 arguments");
      }
      return math::min(args[0], args[1]);
    }
    if (fn == "max") {
      if (args.size() != 2) {
        throw std::runtime_error("max() expects 2 arguments");
      }
      return math::max(args[0], args[1]);
    }
    if (fn == "floor") {
      if (args.size() != 1) {
        throw std::runtime_error("floor() expects 1 argument");
      }
      return math::floor(args[0]);
    }
    if (fn == "ceil") {
      if (args.size() != 1) {
        throw std::runtime_error("ceil() expects 1 argument");
      }
      return math::ceil(args[0]);
    }
    if (fn == "round") {
      if (args.size() != 1) {
        throw std::runtime_error("round() expects 1 argument");
      }
      return math::round(args[0]);
    }
    if (fn == "clamp") {
      if (args.size() != 3) {
        throw std::runtime_error("clamp() expects 3 arguments");
      }
      return math::clamp(args[0], args[1], args[2]);
    }
    throw std::runtime_error("unknown function '" + fn + "'");
  }
  }
  return 0.0;
}

inline void Expression::collectIdentifiers(const Node &node, std::vector<std::string> &out) {
  if (node.kind == Node::Kind::Identifier) {
    for (const std::string &existing : out) {
      if (existing == node.identifier) {
        return; // already collected
      }
    }
    out.push_back(node.identifier);
    return;
  }
  for (const Node &child : node.children) {
    collectIdentifiers(child, out);
  }
}

} // namespace rpg_os

#!/usr/bin/env python3
# Copyright (c) 2026 Christian Mayer and the Mundus Mirabilis contributors.
# SPDX-License-Identifier: Apache-2.0

"""Validates RPG-OS ruleset JSON files against rulesets/ruleset.schema.json.

Uses the `jsonschema` package when installed (full draft-07 validation);
otherwise falls back to stdlib-only structural checks covering the same
essentials: required top-level fields, licence requirement, and the
variant-value forms (integer | dice string | {"min","max"} range) used in the
data sections.

Usage:
    python3 codegen/validate_ruleset.py rulesets/*.json
"""

import json
import re
import sys
from pathlib import Path

SCHEMA_PATH = Path(__file__).resolve().parent.parent / "rulesets" / "ruleset.schema.json"

# A dice/range expression, e.g. "2d6", "1d4+1", "2d6-2", "3d10+6".
_VARIANT_STRING = re.compile(r"^[+-]?([0-9]+d[0-9]+|[0-9]+)([+-][0-9]+d[0-9]+|[+-][0-9]+)*$")


def is_variant_value(node) -> bool:
    """True when `node` is a valid variant value (integer, dice string, range)."""
    if isinstance(node, int) and not isinstance(node, bool):
        return True
    if isinstance(node, str) and _VARIANT_STRING.match(node):
        return True
    if isinstance(node, dict) and set(node) == {"min", "max"}:
        return all(isinstance(v, int) and not isinstance(v, bool) for v in node.values())
    return False


def check_variant_values(obj, path, errors):
    """Recursively checks that numeric data fields are variant values."""
    if isinstance(obj, dict):
        for key, value in obj.items():
            child = f"{path}.{key}"
            if key in ("min", "max") or key in ("attributes", "skills", "resources"):
                # {min,max} are plain ints inside a range object; attribute
                # maps hold variant values.
                if key in ("attributes", "skills", "resources"):
                    for k2, v2 in value.items() if isinstance(value, dict) else []:
                        if not is_variant_value(v2):
                            errors.append(f"{child}.{k2}: not a variant value")
                continue
            check_variant_values(value, child, errors)
    elif isinstance(obj, list):
        for i, item in enumerate(obj):
            check_variant_values(item, f"{path}[{i}]", errors)
    return errors


def structural_check(doc, errors):
    """Stdlib-only structural checks mirroring the schema's essentials."""
    if not isinstance(doc, dict):
        errors.append("root: must be a JSON object")
        return errors
    for key in ("schema_version", "ruleset_id", "licence", "attributes"):
        if key not in doc:
            errors.append(f"root: missing required '{key}'")
    if isinstance(doc.get("licence"), str) and not doc["licence"].strip():
        errors.append("root: 'licence' must be a non-empty string")
    data = doc.get("data")
    if data is not None:
        for section in ("archetypes", "items", "creatures", "spells", "poisons", "diseases", "conditions"):
            if section in data and not isinstance(data[section], list):
                errors.append(f"data.{section}: must be an array")
        check_variant_values(data, "data", errors)
    return errors


def validate(path: Path):
    doc = json.loads(path.read_text())
    schema = json.loads(SCHEMA_PATH.read_text())
    errors = []
    try:
        import jsonschema  # type: ignore

        validator = jsonschema.Draft7Validator(schema)
        errors = [f"{'/'.join(map(str, e.absolute_path))}: {e.message}" for e in validator.iter_errors(doc)]
    except ImportError:
        structural_check(doc, errors)
    return errors


def main() -> int:
    paths = [Path(p) for p in sys.argv[1:]] or sorted((SCHEMA_PATH.parent).glob("*.json"))
    failed = False
    for path in paths:
        if path.name == "ruleset.schema.json":
            continue
        try:
            errors = validate(path)
        except (json.JSONDecodeError, OSError) as exc:
            errors = [str(exc)]
        if errors:
            failed = True
            print(f"{path}: FAILS")
            for error in errors[:20]:
                print(f"  - {error}")
        else:
            print(f"{path}: OK")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())

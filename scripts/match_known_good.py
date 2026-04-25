#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Match a detected Linux config against known-good-configs.json.

Invocation:
    match_known_good.py <configs.json> <distro_id> <distro_version>
                        <kernel_major.minor> <arch>

On stdout, prints three tab-separated fields on one line:
    <status>\t<notes>\t<required_packages_comma_sep>

Where <status> is one of: pass | partial | fail | untested | unknown.
"unknown" means no entry matched at all.

Exit status is always 0 unless arguments are malformed; the classifier
status is communicated via the printed line so the caller can decide how
to weight it.
"""
from __future__ import annotations

import fnmatch
import json
import sys


def ver_tuple(v: str) -> tuple[int, ...]:
    out: list[int] = []
    for p in str(v).split("."):
        try:
            out.append(int(p))
        except ValueError:
            out.append(0)
    return tuple(out) if out else (0,)


def in_range(kmm: str, rng: str | None) -> bool:
    """Return True if kmm (e.g. '6.8') is within the inclusive 'min..max'
    range specified by rng. '*' on either side means unbounded."""
    if not rng or rng in ("*", "*..*"):
        return True
    if ".." not in rng:
        # Exact match
        return kmm == rng
    lo, hi = rng.split("..", 1)
    kt = ver_tuple(kmm)
    if lo and lo != "*":
        if kt < ver_tuple(lo):
            return False
    if hi and hi != "*":
        if kt > ver_tuple(hi):
            return False
    return True


def arch_matches(detected: str, entry_archs: list[str]) -> bool:
    if not entry_archs:
        return True
    # Treat common synonyms as equivalent.
    synonyms = {
        "x86_64": {"x86_64", "amd64"},
        "amd64": {"x86_64", "amd64"},
        "aarch64": {"aarch64", "arm64"},
        "arm64": {"aarch64", "arm64"},
    }
    det_set = synonyms.get(detected, {detected})
    for a in entry_archs:
        if a in det_set:
            return True
    return False


def main(argv: list[str]) -> int:
    if len(argv) != 6:
        print(
            "usage: match_known_good.py <configs.json> <id> <ver> <kmm> <arch>",
            file=sys.stderr,
        )
        return 2

    path, did, dver, kmm, arch = argv[1:6]
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError) as exc:
        print(f"unknown\tfailed to read configs: {exc}\t")
        return 0

    entries = data.get("entries", [])
    best = None
    for e in entries:
        e_id = e.get("distro_id", "")
        if e_id != did and e_id != "*":
            continue
        e_ver = e.get("distro_version", "*")
        if e_ver != "*" and not fnmatch.fnmatchcase(dver, e_ver):
            continue
        if not in_range(kmm, e.get("kernel_range")):
            continue
        if not arch_matches(arch, e.get("arch", [])):
            continue
        # Prefer exact distro_version matches over wildcard ones.
        specificity = 0
        if e_ver == dver:
            specificity += 2
        elif "*" not in e_ver:
            specificity += 1
        if best is None or specificity > best[0]:
            best = (specificity, e)

    if best is None:
        print("unknown\tNo entry in known-good-configs.json matches this system.\t")
        return 0

    e = best[1]
    status = e.get("status", "untested")
    notes = e.get("notes", "")
    req = ",".join(e.get("required_packages", []) or [])
    # Squash tabs/newlines in notes so the one-line protocol is preserved.
    notes = notes.replace("\t", " ").replace("\n", " ").replace("\r", " ")
    print(f"{status}\t{notes}\t{req}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
#!/usr/bin/env python3
"""
compare.py — check a candidate implementation against the golden contract.

This is the parity gate for any reimplementation (a port in another language,
or the C++-kernel refactor). The candidate must, for every case in cases.json,
emit a file <candidate_dir>/<id>.json with the SAME schema the golden files use:

    { "id": "...", "fn": "...", "outputs": { ...same keys as golden... } }

Numbers are compared element-wise with a relative+absolute tolerance — NOT
byte-equality — because a different math library / summation order produces
last-ULP drift that accumulates over the RK4 integration. Strings and booleans
must match exactly. That tolerance IS the written parity guarantee.

Usage:
    ./contract/compare.py <candidate_dir>
    ./contract/compare.py <candidate_dir> --rtol 1e-9 --atol 1e-12
    ./contract/compare.py <candidate_dir> --golden contract/golden

Exit codes:
    0  every case within tolerance — PARITY
    1  at least one case diverges or is missing — FAIL
    2  usage / IO error
"""
import argparse
import json
import math
import os
import sys


def walk_compare(g, c, path, rtol, atol, errors):
    """Recurse golden (g) vs candidate (c) in lockstep, recording mismatches."""
    if isinstance(g, dict):
        if not isinstance(c, dict):
            errors.append(f"{path}: type mismatch (golden object, candidate {type(c).__name__})")
            return
        for k in g:
            if k not in c:
                errors.append(f"{path}.{k}: missing in candidate")
            else:
                walk_compare(g[k], c[k], f"{path}.{k}", rtol, atol, errors)
        for k in c:
            if k not in g:
                errors.append(f"{path}.{k}: extra key in candidate")
    elif isinstance(g, list):
        if not isinstance(c, list):
            errors.append(f"{path}: type mismatch (golden array, candidate {type(c).__name__})")
            return
        if len(g) != len(c):
            errors.append(f"{path}: length {len(g)} (golden) vs {len(c)} (candidate)")
            return
        for i, (gi, ci) in enumerate(zip(g, c)):
            walk_compare(gi, ci, f"{path}[{i}]", rtol, atol, errors)
    elif isinstance(g, bool) or isinstance(c, bool):
        if g != c:
            errors.append(f"{path}: {g!r} (golden) != {c!r} (candidate)")
    elif isinstance(g, (int, float)):
        if not isinstance(c, (int, float)):
            errors.append(f"{path}: numeric golden, non-numeric candidate {c!r}")
            return
        if math.isnan(g) and math.isnan(c):
            return
        diff = abs(float(g) - float(c))
        tol = atol + rtol * abs(float(g))
        if diff > tol:
            rel = diff / abs(float(g)) if g != 0 else float("inf")
            errors.append(f"{path}: {g!r} vs {c!r}  (abs={diff:.3e}, rel={rel:.3e}, tol={tol:.3e})")
    else:  # strings, null
        if g != c:
            errors.append(f"{path}: {g!r} (golden) != {c!r} (candidate)")


def main():
    ap = argparse.ArgumentParser(description="Check a candidate against the golden contract.")
    ap.add_argument("candidate_dir", help="dir of <id>.json files from the candidate implementation")
    ap.add_argument("--golden", default=os.path.join(os.path.dirname(__file__), "golden"),
                    help="golden values dir (default: contract/golden)")
    ap.add_argument("--rtol", type=float, default=1e-9, help="relative tolerance (default 1e-9)")
    ap.add_argument("--atol", type=float, default=1e-12, help="absolute tolerance (default 1e-12)")
    ap.add_argument("--skip", default="", help="comma-separated top-level output keys to exclude "
                    "from comparison (e.g. R-only artifact fields a port does not produce). "
                    "Skipped keys are logged, never silently ignored.")
    args = ap.parse_args()

    skip = {k.strip() for k in args.skip.split(",") if k.strip()}
    if skip:
        print(f"NOTE: skipping output key(s) {sorted(skip)} in every case (--skip)\n")

    golden_files = sorted(f for f in os.listdir(args.golden) if f.endswith(".json"))
    if not golden_files:
        print(f"compare.py: no golden files in {args.golden}", file=sys.stderr)
        return 2

    failures = 0
    for fname in golden_files:
        cid = fname[:-5]
        gpath = os.path.join(args.golden, fname)
        cpath = os.path.join(args.candidate_dir, fname)
        with open(gpath) as fh:
            golden = json.load(fh)
        if not os.path.exists(cpath):
            print(f"FAIL {cid}: candidate file missing ({cpath})")
            failures += 1
            continue
        with open(cpath) as fh:
            cand = json.load(fh)
        g_out = {k: v for k, v in (golden.get("outputs") or {}).items() if k not in skip}
        c_out = {k: v for k, v in (cand.get("outputs") or {}).items() if k not in skip}
        errors = []
        walk_compare(g_out, c_out, "outputs", args.rtol, args.atol, errors)
        if errors:
            failures += 1
            print(f"FAIL {cid}: {len(errors)} mismatch(es) (rtol={args.rtol}, atol={args.atol})")
            for e in errors[:8]:
                print(f"       {e}")
            if len(errors) > 8:
                print(f"       ... and {len(errors) - 8} more")
        else:
            print(f"PASS {cid}")

    print()
    if failures:
        print(f"PARITY FAIL — {failures}/{len(golden_files)} case(s) diverged")
        return 1
    print(f"PARITY OK — all {len(golden_files)} cases within tolerance")
    return 0


if __name__ == "__main__":
    sys.exit(main())

# The `bw` parity contract

A frozen, deterministic statement of **what the canonical `bw` library outputs
for a fixed set of inputs**. It exists so that any reimplementation — a port in
another language, or the C++-kernel refactor — can prove *functional parity*
against the original, not against itself.

```
        cases.json  ──► generate.R (canonical bw, pinned Docker) ──► golden/*.json
                                                                         │
                            ┌────────────────────────────────────────────┤
                            ▼                          ▼                  ▼
                    upstream itself          C++-kernel refactor   port in language X
                  (regen == golden)          (byte-identical)       (compare.py, ε-close)
```

## Files

| File | Role |
|------|------|
| `cases.json`   | **Single source of truth for inputs.** A list of `{id, fn, args}`. Language-neutral JSON — a port reads the same bytes the reference was generated from. |
| `generate.R`   | Authoritative producer. Runs the canonical `bw` against `cases.json`, writes `golden/`. |
| `golden/<id>.json` | The contract. `{id, fn, outputs}`, numbers at 17 significant digits (full IEEE-754 double). Inputs are *not* repeated here — join to `cases.json` by `id`. |
| `Dockerfile`   | Pinned `r-base:4.6.0` environment. The R version is part of the determinism guarantee. |
| `generate.sh`  | Builds the image and regenerates `golden/`. |
| `compare.py`   | Parity gate for a candidate: element-wise tolerance compare against `golden/`. |

## Determinism

The models are pure RK4 integrators with **no randomness** (the `Brownian`
energy method is excluded for that reason). Reproducibility rests on two things:

1. **Pinned toolchain** — regenerating inside the `r-base:4.6.0` image.
2. **`-ffp-contract=off`** — the Dockerfile compiles `bw` with FMA contraction
   disabled. Fused multiply-add (`a*b+c` in a single rounding) is compiler- and
   architecture-dependent; left on, the same source produces different last-bit
   values across toolchains. With it off, the golden is the canonical
   no-fusion result that any same-flag build reproduces bit-for-bit — and it is
   also closer to what a NumPy-based port computes.

Outputs are emitted at 17 significant digits so no precision is lost to
serialization.

## The parity guarantee (epsilon policy)

An exact upstream regen reproduces `golden/` **byte-for-byte** — use `diff`.

Because contraction is pinned off (see Determinism), the **C++-kernel refactor
is also byte-identical** (18/18) when built with the same `-ffp-contract=off`
flag — it is an exact reproduction, not merely a tolerant one. A cross-language
port still cannot be byte-identical: a different math library and summation order
produce last-ULP differences that accumulate over hundreds to thousands of RK4
steps. **A cross-language port must therefore be gated on tolerance, not
byte-equality.** A consumer is in parity when every output element is within
tolerance:

> **abs_err ≤ atol + rtol · |golden|**, with default **rtol = 1e-9**, **atol = 1e-12**.

That inequality is the contract's definition of "the same result." Tighten it
only if your port genuinely holds; loosen it only with a documented reason.

## Using it

**Regenerate / re-cut the contract** (only after a deliberate `cases.json` change):

```bash
./contract/generate.sh           # writes contract/golden/
git add contract && git commit -m "Re-cut parity contract"
git tag contract-vN              # freeze this cut
```

**Check a candidate** — have your implementation read `cases.json`, run each
case, and write `<out>/<id>.json` with the same `{id, fn, outputs}` schema, then:

```bash
./contract/compare.py <out>                         # default tolerance
./contract/compare.py <out> --rtol 1e-9 --atol 1e-12
```

Exit `0` = parity, `1` = a case diverged (mismatches printed), `2` = IO error.

## Cases

18 cases covering: adult model (baseline male/female, explicit EI, weight-loss
deficit via `EIchange`, multi-individual, high PAL, altered carb fraction,
fractional `dt`); child model (1/2/5-year horizons, multi-individual, fractional
`dt`); and the energy-interpolation kernel across all five deterministic methods.
This is a starting contract — add cases to `cases.json` and re-cut as the parity
surface you care about grows.

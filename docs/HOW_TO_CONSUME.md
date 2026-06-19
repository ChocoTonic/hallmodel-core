# How to consume hallmodel-core from another language

This document is the recipe for writing a new language binding. It assumes
familiarity with the target language's FFI (pybind11, `ccall`, Emscripten,
P/Invoke, etc.). The first reference consumer is
[`hallmodel-py`](../../hallmodel-py).

## Step 1 — depend on this repo

Submodule (most common):

```bash
git submodule add https://github.com/<your>/hallmodel-core external/hallmodel-core
git -C external/hallmodel-core checkout <pinned-sha>
```

Pin a specific sha. The parity guarantee is rebuilt against your pinned sha
only — bumping the submodule = re-running parity.

## Step 2 — build the kernel

Either link the prebuilt static lib (`libbw_core.a`) or `add_subdirectory`
the kernel into your CMake build. Use the same `-ffp-contract=off` flag.
Floating-point determinism depends on it.

## Step 3 — write the marshalling shim

The kernel exposes three classes. Mirror the dispatch pattern in
`bindings/bw_cpp.cpp` (in `hallmodel-py`) for the FFI of your choice.

### Adult

Three constructors corresponding to whether `EI` and/or `fat` are user-supplied:

```cpp
// Baseline (Mifflin-St Jeor):
bw::Adult(weight, height, age_yrs, sex, EIchange, NAchange,
          PAL, percentc, percentb, dt, checkValues);

// One of EI or fat supplied (controlled by isEnergy bool):
bw::Adult(weight, height, age_yrs, sex, EIchange, NAchange,
          PAL, percentc, percentb, dt, extradata, checkValues, isEnergy);

// Both supplied:
bw::Adult(weight, height, age_yrs, sex, EIchange, NAchange,
          PAL, percentc, percentb, dt, input_EI, input_fat, checkValues);
```

- `sex`: `0.0` = male, `1.0` = female.
- `EIchange`, `NAchange`: `bw::Matrix` with `nrow = ceil(days/dt)`, `ncol = n_individuals`. Rows are days, cols are individuals. **Note the transpose** — the user-facing convention is `(individual, day)` and the kernel takes `(day, individual)`.
- `percentc`: current carb fraction. `percentb`: baseline carb fraction. If your user passes only `pcarb_base`, copy it to `pcarb`.

Then `auto result = model.rk4(ceil(days));`.

### Child

Two constructors — classic (with explicit EI matrix) or Richardson generalized logistic:

```cpp
bw::Child(age, sex, FFM, FM, EIntake, ei_nrows, ei_ncols, dt, checkValues);
bw::Child(age, sex, FFM, FM, K, Q, A, B, nu, C, dt, checkValues);
```

`EIntake` is row-major: `data[r*ei_ncols + c]`, rows are time slots,
columns are individuals.

Defaults for `FFM` / `FM`: construct a throwaway `Child` and call
`.FMReference(age)` / `.FFMReference(age)`. Defaults for `EI`: see
`tests/parity_runner.cpp:child_reference_EI` for the exact pattern (note
that R's wrapper swaps `FFM`/`FM` argument positions when computing
reference EI — that quirk is part of the parity claim, do not "fix" it).

Then `auto result = model.rk4(days - 1);` (the `-1` is the R-wrapper
convention; it matches the upstream golden).

### Energy

Free functions in `bw::energy`:

```cpp
bw::energy::Method m;
bw::energy::parse_method("Linear", &m);
size_t ncols = bw::energy::output_ncols(time_data, time_size);
std::vector<double> evals(nrow * ncols);
bw::energy::build_deterministic(energy_col_major, nrow, ntimes,
                                time_data, m, evals.data());
```

`Energy` is **column-major**: `Energy[r + c * nrow]`. The output `Evalues`
is in the same column-major layout. `"Brownian"` is not implemented in the
kernel because it needs an RNG; handle it binding-side if you need it.

## Step 4 — adopt the contract as your test suite

`contract/cases.json` + `contract/golden/` are language-independent. Your
binding's test suite should:

1. Read `cases.json`.
2. Drive each case through your binding.
3. Emit `<id>.json` files in the schema `contract/compare.py` expects.
4. Shell out to `contract/compare.py` (or port its tolerance check
   in-process).

`hallmodel-py/tests/test_contract_parity.py` is the worked example.

## What you must NOT do

- Reorder floating-point operations — last-ULP drift accumulates over
  hundreds of RK4 steps and breaks the tolerance.
- Drop `-ffp-contract=off` — same reason.
- Pre-process the `cases.json` data outside what the kernel expects —
  the contract pins the inputs at one place; convenience reshapes belong
  in your binding, not in the data.
- "Fix" the upstream quirks documented in `proof/verify.sh` (the
  `--skip BMI_Category`, the `FFM/FM` arg swap in `child_reference_EI`,
  the `rk4(days - 1)` for child). They are part of the parity claim.

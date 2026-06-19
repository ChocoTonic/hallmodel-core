# hallmodel-core

A portable C++ implementation of the [Hall](https://www.niddk.nih.gov/about-niddk/staff-directory/biography/hall-kevin)
adult- and child- body-weight dynamics models, packaged as a dependency-free
static library with a mechanically-verified parity proof against the canonical
R reference.

The math comes from the R package [`INSP-RH/bw`](https://github.com/INSP-RH/bw)
(itself a faithful reimplementation of Hall et al.'s published differential
equations). This repo extracts the C++ core into a form that is consumable
from any language with a C++ FFI — Python, Julia, WASM, C# — without dragging
in R or Rcpp.

**The original R package is the source of truth for the model.** This repo
exists so other languages can use the same math without forking it. See
[ATTRIBUTION.md](docs/ATTRIBUTION.md).

---

## What's in here

```
include/bw/        # the public interface — three headers, no Rcpp
src/               # the implementation — three .cpp files, plain C++17
contract/          # vendored verbatim from INSP-RH/bw's `contract` branch
proof/             # frozen evidence the kernel passes that contract
tests/parity_runner.cpp  # builds against bw_core, drives the contract
```

`include/bw/{adult,child,energy}.hpp` is the entire public surface. They
include only `<cstddef>`, `<string>`, `<vector>` — no third-party headers, no
R, no Python, no NumPy.

## The parity claim

The kernel is verified against the upstream parity contract
(`contract/cases.json` + `contract/golden/`, authored by the maintainer of
[`INSP-RH/bw`](https://github.com/INSP-RH/bw)). For all 18 cases — adult
baseline, adult with explicit EI / deficit / multi-individual / high PAL /
altered carb fraction / fractional dt; child at 1, 2, and 5-year horizons,
multi-individual and fractional dt; and all five deterministic energy
interpolation methods — every numeric output matches the R reference within
the contract's tolerance (`rtol=1e-9`, `atol=1e-12`).

The one excluded field is `BMI_Category`: the contract's `generate.R`
serializes character matrices through `as.numeric()`, which coerces every
label to `NA`, so the golden's `BMI_Category` is a column of literal `"NA"`
strings. The kernel correctly computes the WHO BMI bins; we skip the field
rather than reproduce the upstream coercion artifact. See
[`proof/verify.sh`](proof/verify.sh) for the documented `--skip`.

**Run the proof yourself:**

```bash
./proof/verify.sh
```

This builds `parity_runner`, executes every case in `contract/cases.json`
against the kernel, and runs the contract's own `compare.py` against
`contract/golden/`. Exit 0 means parity holds.

**Read the proof without building:**

[`proof/last_run.log`](proof/last_run.log) is the committed log of the most
recent green run; [`proof/outputs/`](proof/outputs/) contains the 18 result
JSONs that were diffed against `contract/golden/`. CI re-derives both on
every PR.

## Using it from C++

CMake `find_package` (after `cmake --install`):

```cmake
find_package(hallmodel CONFIG REQUIRED)
target_link_libraries(myapp PRIVATE hallmodel::core)
```

Or as a submodule:

```cmake
add_subdirectory(external/hallmodel-core EXCLUDE_FROM_ALL)
target_link_libraries(myapp PRIVATE hallmodel::core)
```

Then:

```cpp
#include <bw/adult.hpp>
#include <bw/child.hpp>
#include <bw/energy.hpp>

bw::Matrix EIc(/*days=*/365, /*nind=*/1, 0.0);
bw::Matrix NAc(365, 1, 0.0);
bw::Adult model(
    /*weight=*/{76.0}, /*height=*/{1.73}, /*age=*/{36.0}, /*sex=*/{0.0},
    EIc, NAc,
    /*PAL=*/{1.5}, /*percentc=*/{0.5}, /*percentb=*/{0.5},
    /*dt=*/1.0, /*check=*/true);
bw::AdultResult r = model.rk4(365.0);
```

See [`docs/HOW_TO_CONSUME.md`](docs/HOW_TO_CONSUME.md) for the full guide
including conventions (sex encoding, `EIchange` transposition, etc.).

## Using it from another language

The first reference consumer is **[hallmodel-py](../hallmodel-py)** — a
pybind11 wrapper that pins this repo as a submodule and proves the
"interface for other libraries" claim is real. It is the recipe to
follow for a Julia / WASM / C# port:

1. Vendor this repo as a submodule.
2. Write a thin shim (pybind11 / `ccall` + C wrapper / Emscripten / P/Invoke)
   over the three classes.
3. Re-use `contract/cases.json` + `golden/` as your test suite.

The parity gate is language-independent: it lives in the contract files, not
in C++ or Python.

## Build requirements

- CMake ≥ 3.16
- C++17 compiler
- Python 3 (only for the verification step — not for using the library)

The library itself is C++17 + STL; nothing else.

## Floating-point determinism

The CMakeLists pins `-ffp-contract=off` — the same flag the upstream R
package uses (`src/Makevars` in `INSP-RH/bw`). Without it, fused multiply-add
on `a*b+c` fuses into a single rounding that varies across compilers and
architectures, and the same source produces last-bit-different outputs. With
it off, the kernel is bit-for-bit reproducible across any same-flag build,
and the contract tolerance is hit comfortably (in practice byte-identical to
upstream R when built in matched toolchains).

## License

MIT, carried forward from [`INSP-RH/bw`](https://github.com/INSP-RH/bw). See
[LICENSE.md](LICENSE.md).

## Citing

If you use this in research, please cite the original `bw` package and
Hall's underlying papers. The README of
[`INSP-RH/bw`](https://github.com/INSP-RH/bw) lists the canonical references.

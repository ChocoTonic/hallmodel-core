# Attribution

`hallmodel-core` is a **port**, not original research. Every line of math in
`src/` traces to a published model and a prior implementation.

## The model

The adult and child body-weight dynamics are from Kevin D. Hall et al.'s
published differential equations:

- Hall, K.D., et al. (2011). *Quantification of the effect of energy
  imbalance on bodyweight.* The Lancet, 378(9793), 826-837.
- Hall, K.D. (2010). *Predicting metabolic adaptation, body weight change,
  and energy intake in humans.* American Journal of Physiology -
  Endocrinology and Metabolism, 298(3), E449-E466.
- Hall, K.D., et al. (2013). *Dynamics of childhood growth and obesity:
  development and validation of a quantitative mathematical model.* The
  Lancet Diabetes & Endocrinology, 1(2), 97-105.

The README of [`INSP-RH/bw`](https://github.com/INSP-RH/bw) carries the
canonical, maintainer-blessed citation list — defer to that when citing in
publications.

## The implementation

The C++ in `src/` is translated from the C++ in `INSP-RH/bw`'s `src/` (the
maintainer-authored Rcpp implementation). The translation is mechanical:
container types changed from `Rcpp::NumericVector` / `Rcpp::NumericMatrix`
to `std::vector<double>` / `bw::Matrix`, arithmetic preserved verbatim —
operation order, parenthesization, constants, and the sequence of RK4
sub-integrations are unchanged. The parity contract in `contract/` (also
authored by `INSP-RH/bw`'s maintainer) is the mechanical proof of that.

## The contract

`contract/` is vendored verbatim from
[`INSP-RH/bw`'s `contract` branch](https://github.com/INSP-RH/bw/tree/contract/contract).
We did not invent the parity gate; we trust the upstream author's
definition of "the same result" (tolerance, case coverage, output schema)
and re-use their `compare.py` unchanged.

## What is original here

- The repo layout (separating the kernel from R-package shape).
- `tests/parity_runner.cpp` — the C++ program that drives the contract
  cases through the kernel and writes outputs in the schema `compare.py`
  expects. This mirrors the orchestration in `bw-chocotonic`'s Python
  shim (`bw_cpp/__init__.py`).
- `CMakeLists.txt`, `proof/verify.sh`, CI plumbing.

## How to credit this work

- Cite the Hall papers (or the `INSP-RH/bw` README's citation list) for the
  *model*.
- Cite `INSP-RH/bw` for the *reference implementation* and the parity
  contract.
- Optionally reference this repo as "a portable C++ port of the bw kernel"
  if it helped your work — but the upstream package is the canonical
  artifact.

We are grateful to [@RodrigoZepeda](https://github.com/RodrigoZepeda) and
the INSP-RH team for publishing the model, the implementation, and the
contract under MIT, all of which made this port possible.

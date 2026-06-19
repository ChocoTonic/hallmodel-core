#!/usr/bin/env Rscript
# ---------------------------------------------------------------------------
# generate.R — produce the parity CONTRACT golden values from the canonical
# (upstream) bw package.
#
# This script is the authoritative producer of the contract. It is run inside
# the pinned Docker image (see Dockerfile / generate.sh) against the ORIGINAL
# bw source, so the numbers it emits are the reference every other
# implementation — the C++-kernel refactor, and any from-scratch port in
# another language — must reproduce.
#
# Inputs : cases.json   (language-neutral input manifest, the single source of
#                         truth for what each case feeds the model)
# Outputs: golden/<id>.json = { id, fn, outputs }
#          numbers at 17 significant digits = full IEEE-754 double precision.
#
# Inputs live ONLY in cases.json; golden files carry outputs only. Consumers
# join the two by `id`. This guarantees there is exactly one statement of each
# input, so a port reads the same bytes the reference was generated from.
# ---------------------------------------------------------------------------

suppressMessages({
  library(bw)
  library(jsonlite)
})

work       <- Sys.getenv("CONTRACT_DIR", unset = "/work")
cases_path <- file.path(work, "cases.json")
out_dir    <- file.path(work, "golden")
dir.create(out_dir, recursive = TRUE, showWarnings = FALSE)

# simplifyMatrix turns a JSON [[...]] (array-of-arrays) into an R matrix, which
# is exactly what EIchange / NAchange / the energy matrix need. Scalars stay
# length-1 vectors; flat arrays stay vectors. simplifyDataFrame=FALSE keeps the
# top-level case list as a plain list of objects.
cases <- fromJSON(cases_path, simplifyVector = TRUE,
                  simplifyMatrix = TRUE, simplifyDataFrame = FALSE)

# Turn a model result (named list of scalars / vectors / matrices) into a
# JSON-friendly structure. Matrices become a list of numeric row vectors (one
# row per individual); scalar character/logical flags pass through; everything
# else is coerced to a plain numeric vector.
serialize <- function(model) {
  lapply(model, function(x) {
    if (is.matrix(x)) {
      lapply(seq_len(nrow(x)), function(i) as.numeric(x[i, ]))
    } else if (is.character(x) && length(x) == 1) {
      x
    } else if (is.logical(x) && length(x) == 1) {
      x
    } else {
      as.numeric(x)
    }
  })
}

run_case <- function(case) {
  fn <- case$fn
  a  <- case$args
  if (fn == "adult_weight") {
    serialize(do.call(adult_weight, a))
  } else if (fn == "child_weight") {
    serialize(do.call(child_weight, a))
  } else if (fn == "EnergyBuilder") {
    # Call the C++ kernel directly (the energy_build() R wrapper drops the
    # first column); the contract captures the kernel's full output.
    res <- bw:::EnergyBuilder(a$energy, a$time, a$method)
    list(energy = lapply(seq_len(nrow(res)), function(i) as.numeric(res[i, ])))
  } else {
    stop(sprintf("Unknown fn '%s' in case '%s'", fn, case$id))
  }
}

cat(sprintf("Generating %d golden files from canonical bw ...\n", length(cases)))
for (case in cases) {
  cat(sprintf("  %-26s (%s)\n", case$id, case$fn))
  outputs <- run_case(case)
  write_json(list(id = case$id, fn = case$fn, outputs = outputs),
             file.path(out_dir, paste0(case$id, ".json")),
             auto_unbox = TRUE, digits = 17, pretty = FALSE)
}
cat(sprintf("Done. %d golden files in %s\n",
            length(list.files(out_dir, pattern = "\\.json$")), out_dir))

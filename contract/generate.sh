#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# generate.sh — (re)generate the golden contract values inside the pinned image.
#
# Builds the canonical bw package in r-base:4.6.0 and runs generate.R against
# cases.json, writing contract/golden/. Run this only when you deliberately
# change cases.json or intend to re-cut the contract (then re-tag it).
#
# Usage:  ./contract/generate.sh
# Exit 0 = golden values written to contract/golden/.
# ---------------------------------------------------------------------------
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." >/dev/null 2>&1 && pwd)"
IMAGE="bw-contract:latest"

if ! docker info >/dev/null 2>&1; then
  echo "generate.sh: docker is not running" >&2
  exit 2
fi

echo ">> building $IMAGE (pinned r-base:4.6.0) from $REPO_ROOT"
docker build -f "$SCRIPT_DIR/Dockerfile" -t "$IMAGE" "$REPO_ROOT"

echo ">> generating golden values into $SCRIPT_DIR/golden"
docker run --rm \
  -e CONTRACT_DIR=/work \
  -v "$SCRIPT_DIR:/work" \
  "$IMAGE" \
  Rscript /work/generate.R

echo ">> done. golden values in $SCRIPT_DIR/golden"

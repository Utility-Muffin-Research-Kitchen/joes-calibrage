#!/usr/bin/env bash
# Cross-build Joe's Calibrage for MLP1 inside the mlp1-toolchain container, with
# the UMRK workspace mounted so Catastrophe and Jawaka resolve as siblings.
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
WORKSPACE="$(cd "$APP_DIR/.." && pwd)"   # siblings: joes-calibrage, Catastrophe, Jawaka
IMAGE="${MLP1_TOOLCHAIN_IMAGE:-ghcr.io/utility-muffin-research-kitchen/mlp1-toolchain:latest}"

echo "=== Building Joe's Calibrage for MLP1 (workspace: $WORKSPACE) ==="
docker run --rm -v "$WORKSPACE":/workspace -w /workspace/joes-calibrage "$IMAGE" \
	make -C ports/mlp1

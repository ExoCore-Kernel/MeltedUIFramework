#!/usr/bin/env bash
set -euo pipefail

submodule_path="${1:-external/MeltedGlassOpenGL}"

if [[ ! -f .gitmodules ]]; then
  echo "Run this script from the MeltedUIFramework repository root." >&2
  exit 1
fi

git submodule sync -- "$submodule_path"
git submodule update --init --remote -- "$submodule_path"

git -C "$submodule_path" rev-parse HEAD

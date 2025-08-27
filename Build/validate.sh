#!/bin/bash
set -e
PROJ_DIR="$(cd "$(dirname "$0")/.."; pwd)"
UPROJECT="$PROJ_DIR/Skald.uproject"

echo "Running compile check..."
if command -v UnrealBuildTool &>/dev/null; then
  UnrealBuildTool Development Linux -Project="$UPROJECT" -TargetType=Editor SkaldEditor -Quiet || exit 1
else
  echo "UnrealBuildTool not found; skipping compile check." >&2
fi

echo "Running automation tests..."
if command -v UnrealEditor &>/dev/null; then
  UnrealEditor "$UPROJECT" -ExecCmds="Automation RunTests Skald.UI.BindingsRemain;Quit" -unattended -nop4 || exit 1
else
  echo "UnrealEditor not found; cannot run tests." >&2
fi

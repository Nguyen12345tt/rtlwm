#!/usr/bin/env bash
# load.sh – Load rtlwm kext for testing
#
# Must be run as root.  Requires a macOS system with SIP partially disabled
# or a kernel debug kit environment.
#
# Adapted from OpenIntelWireless/itlwm scripts/load.sh

set -euo pipefail

KEXT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RTLWM_KEXT="$KEXT_DIR/build/Debug/rtlwm.kext"

if [ ! -d "$RTLWM_KEXT" ]; then
    echo "Error: $RTLWM_KEXT not found.  Build the project first."
    exit 1
fi

echo "Loading rtlwm…"
sudo kextload "$RTLWM_KEXT"
echo "Done. Check Console.app / 'log show --predicate \"eventMessage CONTAINS[cd] \\\"rtlwm\\\"\"' for messages."

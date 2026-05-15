#!/usr/bin/env bash
# load.sh – Load Airportrtlwm kext for testing
#
# Must be run as root.  Requires a macOS system with SIP partially disabled
# or a kernel debug kit environment.
#
# Adapted from OpenIntelWireless/itlwm scripts/load.sh

set -euo pipefail

KEXT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
AIRPORTRTLWM="$KEXT_DIR/build/Debug/Airportrtlwm.kext"

if [ ! -d "$AIRPORTRTLWM" ]; then
    echo "Error: $AIRPORTRTLWM not found.  Build the project first."
    exit 1
fi

echo "Loading Airportrtlwm…"
sudo kextload "$AIRPORTRTLWM"
echo "Done.  Check Console.app / 'log show --predicate …rtlwm…' for messages."

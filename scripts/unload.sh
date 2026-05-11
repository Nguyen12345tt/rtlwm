#!/usr/bin/env bash
# unload.sh – Unload Airportrtlwm kext
#
# Must be run as root.
#
# Adapted from OpenIntelWireless/itlwm scripts/unload.sh

set -euo pipefail

echo "Unloading Airportrtlwm…"
sudo kextunload -b com.rtlwm.airportrtlwm && echo "Done." || true

#!/usr/bin/env bash
# unload.sh – Unload rtlwm kext
#
# Must be run as root.
#
# Adapted from OpenIntelWireless/itlwm scripts/unload.sh

set -euo pipefail

echo "Unloading rtlwm…"
sudo kextunload -b com.rtlwm.rtlwm && echo "Done." || true

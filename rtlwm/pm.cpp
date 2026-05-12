/*
 * pm.cpp – Power management for rtlwm
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "rtlwm.hpp"

/*
 * System sleep/wake notifications arrive via setPowerState().
 * The actual hardware sleep/wake sequences are handled in the HAL
 * (hal_rtw88 or hal_rtw89) via enable() / disable().
 *
 * Full S3/S4 wake sequencing will be implemented when HAL power hooks land.
 */

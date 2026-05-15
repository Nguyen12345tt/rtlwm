/*
 * net80211_rtl.h – Shim connecting OpenBSD net80211 stack to RTL HAL
 *
 * This file bridges the OpenBSD-derived ieee80211_var structures used by the
 * 802.11 management layer to the RTL hardware driver (hal_rtw88 / hal_rtw89).
 *
 * The OpenBSD net80211 headers (ieee80211_var.h, ieee80211_mira.h, etc.) are
 * not included here directly because they ship as part of the macOS KDK.
 * Add the KDK path to your Xcode Header Search Paths.
 */

#ifndef net80211_rtl_h
#define net80211_rtl_h

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_amrr.h>
#include <net80211/ieee80211_mira.h>
#include <net80211/ieee80211_proto.h>
#include <net80211/ieee80211_radiotap.h>

/* RTL-specific 802.11 extensions ----------------------------------------- */

/* Maximum number of simultaneously tracked RTL stations */
#define RTL_MAX_STATIONS    64

/* TX queue indices (mirrors Linux rtw88 / rtw89 priority levels) */
enum rtl_txq {
    RTL_TXQ_VO = 0,    /* Voice (highest) */
    RTL_TXQ_VI,        /* Video */
    RTL_TXQ_BE,        /* Best Effort */
    RTL_TXQ_BK,        /* Background */
    RTL_TXQ_MGMT,      /* Management */
    RTL_TXQ_BCN,       /* Beacon */
    RTL_TXQ_NUM
};

#endif /* net80211_rtl_h */

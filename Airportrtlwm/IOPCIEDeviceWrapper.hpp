/*
 * IOPCIEDeviceWrapper.hpp – Lightweight PCIE device helpers
 *
 * Provides convenience wrappers used to read/write PCI configuration space
 * and map device memory in a style consistent with the OpenBSD compat layer.
 *
 * Adapted from OpenIntelWireless/itlwm (GPLv2)
 */

#ifndef IOPCIEDeviceWrapper_hpp
#define IOPCIEDeviceWrapper_hpp

#include <IOKit/pci/IOPCIDevice.h>

/* Convenience: read a 32-bit register from MMIO base + offset */
static inline uint32_t
rtl_read32(volatile uint8_t *base, uint32_t offset)
{
    return *(volatile uint32_t *)(base + offset);
}

/* Convenience: write a 32-bit register at MMIO base + offset */
static inline void
rtl_write32(volatile uint8_t *base, uint32_t offset, uint32_t value)
{
    *(volatile uint32_t *)(base + offset) = value;
    OSSynchronizeIO();
}

static inline uint16_t
rtl_read16(volatile uint8_t *base, uint32_t offset)
{
    return *(volatile uint16_t *)(base + offset);
}

static inline void
rtl_write16(volatile uint8_t *base, uint32_t offset, uint16_t value)
{
    *(volatile uint16_t *)(base + offset) = value;
    OSSynchronizeIO();
}

static inline uint8_t
rtl_read8(volatile uint8_t *base, uint32_t offset)
{
    return *(volatile uint8_t *)(base + offset);
}

static inline void
rtl_write8(volatile uint8_t *base, uint32_t offset, uint8_t value)
{
    *(volatile uint8_t *)(base + offset) = value;
    OSSynchronizeIO();
}

#endif /* IOPCIEDeviceWrapper_hpp */

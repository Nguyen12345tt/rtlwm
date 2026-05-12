# rtlwm Porting Status (Linux Realtek -> macOS)

Repository đã mirror cây thư mục theo cấu trúc itlwm:

- `Airportrtlwm/` (Airport interface layer)
- `include/` (Airport + HAL shared headers)
- `rtl80211/` (Linux/OpenBSD compatibility layer)
- `rtlwm/` (non-Airport core + RTW88/RTW89 HAL)
- `scripts/` (firmware generate/load/unload)

## Đã làm

1. Khung driver cho 2 họ chip Linux Realtek:
   - `rtw88` -> `rtlwm/hal_rtw88`
   - `rtw89` -> `rtlwm/hal_rtw89`
2. PCI ID matching đã có trong:
   - `Airportrtlwm/Info.plist`
   - `rtlwm/Info.plist`
   - `rtlwm/rtlwm.cpp` (`probeHardware`)
3. Pipeline firmware nhúng vào kext:
   - `scripts/fw_gen.sh`
   - `include/FwData.h`

## Tổng hợp theo danh sách Linux Realtek đã cung cấp

- Đang có lớp HAL trong repo này:
  - `rtw88`
  - `rtw89`
- Chưa có lớp HAL tương ứng trong repo này (chưa port):
  - `rtl818x` (`rtl8180`, `rtl8187`)
  - `rtl8xxxu`
  - `rtlwifi/*` (`rtl8188ee`, `rtl8192*`, `rtl8723*`, `rtl8821ae`, `btcoexist`, ...)

Ghi chú: các họ chưa port ở trên cần thêm mapping chip, firmware path, và HAL mới trước khi có thể dùng trên macOS.

## Còn thiếu để card chạy thực tế trên macOS

1. Port logic MAC/PHY/PCI đầy đủ từ Linux (`rtw88`/`rtw89`) vào HAL `.cpp`.
2. Hoàn thiện đường TX/RX, interrupt, DMA descriptors.
3. Hoàn thiện IOCTL handlers trong `AirportSTAIOCTL.cpp` và `AirportVirtualIOCTL.cpp`.
4. Hoàn thiện glue với `ieee80211` stack và state machine kết nối.
5. Build và ký kext trên macOS + KDK phù hợp.

## Mapping Linux -> macOS

Xem file: `linux_realtek_map.json`.

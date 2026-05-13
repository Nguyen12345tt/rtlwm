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
4. Hoàn thiện IOCTL handlers trong `AirportSTAIOCTL.cpp` và `AirportVirtualIOCTL.cpp`:
   - Bổ sung đầy đủ các GET handler: `STATUS_DEV_NAME`, `HT_CAPABILITY`, `VHT_CAPABILITY`, `GUARD_INTERVAL`, `BLOCK_ACK`, `PHY_SUB_MODE`, v.v.
   - Bổ sung đầy đủ các SET handler: `REASSOCIATE`, `CHANNEL`, `TX_ANTENNA`, `RX_ANTENNA`, `INT_MIT`, `GUARD_INTERVAL`, `BLOCK_ACK`, `PHY_SUB_MODE`, v.v.
   - Sửa dữ liệu trả về RSSI/noise (điền đầy đủ các trường `_ext` và `aggregate_*_ext`).
   - Virtual interface: delegate toàn bộ GET/SET tương thích sang STA handler; giữ `EOPNOTSUPP` cho `VIRTUAL_IF_CREATE/DELETE/ROLE/PARENT`.

## Tổng hợp theo danh sách Linux Realtek đã cung cấp

- Đang có lớp HAL trong repo này:
  - `rtw88`
  - `rtw89`
- Chưa có lớp HAL tương ứng trong repo này (chưa port):
  - `rtl818x` (`rtl8180`, `rtl8187`)
  - `rtl8xxxu`
  - `rtlwifi/*` (`rtl8188ee`, `rtl8192*`, `rtl8723*`, `rtl8821ae`, `btcoexist`, ...)

Ghi chú: các họ chưa port ở trên cần thêm mapping chip, firmware path, và HAL mới trước khi có thể dùng trên macOS.

## Việc cần làm tiếp theo (TODO)

- [ ] Hoàn thiện phần MAC/PHY/PCI còn thiếu cho từng chip `rtw88`/`rtw89` để đủ mức chạy thực tế, không chỉ bring-up.
- [ ] Hoàn thiện TX/RX data path dưới tải:
  - [ ] xử lý đầy đủ interrupt path theo queue,
  - [ ] hoàn thiện luồng RX parse/recycle/error path,
  - [ ] bổ sung các case TX thực tế (retry/fail/reclaim đồng bộ).
- [ ] Hoàn thiện glue với `ieee80211` stack và state machine kết nối (scan -> auth -> assoc -> data -> disconnect/reconnect).
- [ ] Kiểm thử ổn định thực tế trên phần cứng:
  - [ ] reconnect nhiều lần, sleep/wake, roaming cơ bản,
  - [ ] stress test ping/throughput dài hạn.
- [ ] Hoàn thiện build + ký kext trên macOS với KDK phù hợp, và chốt checklist release/debug.

## Mapping Linux -> macOS

Xem file: `linux_realtek_map.json`.

## Link Linux/Realtek tham chiếu nhanh

- Linux Realtek wireless tree:
  - https://github.com/torvalds/linux/tree/master/drivers/net/wireless/realtek
- RTW88:
  - https://github.com/torvalds/linux/tree/master/drivers/net/wireless/realtek/rtw88
- RTW89:
  - https://github.com/torvalds/linux/tree/master/drivers/net/wireless/realtek/rtw89
- Linux firmware (nguồn `.bin`):
  - https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git

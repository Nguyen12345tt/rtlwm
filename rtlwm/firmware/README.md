# Firmware directory
#
# Realtek firmware files (.bin) used by rtlwm MUST be placed here.
# They are compressed with zlib and embedded into the kext bundle
# by the fw_gen.sh script during the build phase.
#
# Required firmware files (download from linux-firmware.git):
#
#  RTW88 family:
#   rtw8822b_fw.bin
#   rtw8822c_fw.bin
#   rtw8723d_fw.bin
#   rtw8821c_fw.bin
#
#  RTW89 family:
#   rtw8852a_fw.bin
#   rtw8852b_fw.bin
#   rtw8851b_fw.bin
#   rtw8852c_fw.bin
#   rtw8922a_fw.bin
#
# Source: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git
# Path:   realtek/
#
# After placing the .bin files here, run:
#   scripts/fw_gen.sh
# to generate the fw_list.cpp that is compiled into the kext.

# Bulk Linux firmware mirror import
#
# Snapshot firmware from:
#   https://github.com/CirrusLogic/linux-firmware
# is stored under:
#   rtlwm/firmware/linux-firmware/{rtw88,rtw89,rtlwifi,rtl_nic}
#
# This keeps upstream firmware trees intact for reference.

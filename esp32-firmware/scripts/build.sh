#!/usr/bin/env bash
# scripts/build.sh — 一键：准备 ESP-IDF → 编译 → 合并单 bin
# 用法：bash scripts/build.sh
# 依赖：git、python3；若 ~/esp/esp-idf 不存在则自动浅克隆 v5.3 并 install esp32。
set -euo pipefail

IDF_DIR="${IDF_DIR:-$HOME/esp/esp-idf}"
PROJ_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJ_DIR"

# 1. 准备 ESP-IDF
if [ ! -f "$IDF_DIR/export.sh" ]; then
  echo "[*] 未发现 $IDF_DIR，浅克隆 ESP-IDF v5.3 ..."
  git clone --depth 1 --branch v5.3 https://github.com/espressif/esp-idf.git "$IDF_DIR"
  cd "$IDF_DIR" && ./install.sh esp32 && cd "$PROJ_DIR"
fi
# shellcheck disable=SC1090
source "$IDF_DIR/export.sh"
echo "[*] idf.py: $(idf.py --version)"

# 2. 配置目标 + 编译
idf.py set-target esp32
idf.py build

# 3. 合并为单 bin
mkdir -p dist
python3 -m esptool --chip esp32 merge_bin \
  -o dist/esp32-miwear-bridge-v1.0.0.bin \
  --flash-mode dio --flash-freq 40m --flash-size 4MB \
  0x1000 build/bootloader/bootloader.bin \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/miwear_esp_bridge.bin

echo "[*] 产物：$PROJ_DIR/dist/esp32-miwear-bridge-v1.0.0.bin"
sha256sum dist/esp32-miwear-bridge-v1.0.0.bin

# 4. 烧录（插上设备后取消下一行注释；按你历史方式 offset 0x0 / 921600）
# python3 -m esptool --chip esp32 -p /dev/ttyUSB0 -b 921600 \
#   --before default_reset --after hard_reset \
#   write_flash 0x0 dist/esp32-miwear-bridge-v1.0.0.bin

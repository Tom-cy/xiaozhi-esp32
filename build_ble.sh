#!/usr/bin/env bash
# ============================================================
# 构建 bread-compact-wifi BLE 配网版固件
# 输出: build/merged-binary.bin  (烧录文件)
# ============================================================

#每次打开终端激活环境
# source ~/esp/esp-idf/export.sh
# 构建固件
# ./build_ble.sh
set -e

BOARD="bread-compact-wifi"
VARIANT="bread-compact-wifi-ble"
TARGET="esp32s3"

# ── 颜色输出 ────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*"; exit 1; }

# ── 切换到项目根目录 ─────────────────────────────────────────
cd "$(dirname "$0")"
info "工作目录: $(pwd)"

# ── 检查 ESP-IDF 环境 ────────────────────────────────────────
if ! command -v idf.py &>/dev/null; then
    echo ""
    warn "未检测到 idf.py，请先安装并激活 ESP-IDF 环境："
    echo ""
    echo "  # macOS 一键安装（约 2-3 分钟）："
    echo "  mkdir -p ~/esp && cd ~/esp"
    echo "  git clone --recursive https://github.com/espressif/esp-idf.git"
    echo "  cd esp-idf && git checkout v5.3.2"
    echo "  ./install.sh esp32s3"
    echo ""
    echo "  # 每次打开终端都需要执行："
    echo "  source ~/esp/esp-idf/export.sh"
    echo ""
    echo "  # 然后重新运行本脚本"
    exit 1
fi

IDF_VER=$(idf.py --version 2>&1 | head -1)
info "ESP-IDF: $IDF_VER"

# ── 清理旧构建（可选，加 --clean 参数触发）───────────────────
if [[ "$1" == "--clean" ]]; then
    warn "清理旧构建目录..."
    rm -rf build sdkconfig
fi

# ── 设置目标芯片 ─────────────────────────────────────────────
info "设置目标芯片: $TARGET"
idf.py set-target "$TARGET"

# ── 生成 BLE 专用 sdkconfig defaults ────────────────────────
# 不能直接追加 sdkconfig：BT 控制器有大量依赖子配置需要 Kconfig 解析才能补全
# 正确做法：写入 defaults 文件，由 ESP-IDF 构建系统自动解析所有依赖
info "生成 sdkconfig.defaults.ble..."
cat > sdkconfig.defaults.ble << 'EOF'
# BLE 配网变体专用配置（bread-compact-wifi-ble）
CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI=y
CONFIG_OLED_SSD1306_128X32=y
CONFIG_USE_HOTSPOT_WIFI_PROVISIONING=n
CONFIG_USE_SIMPLE_BLE_PROVISIONING=y

# BLE / NimBLE（由 Kconfig 依赖解析自动补全 HCI/coex 等子配置）
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y

# 自定义唤醒词（小鹿小鹿）
CONFIG_USE_CUSTOM_WAKE_WORD=y
CONFIG_CUSTOM_WAKE_WORD="xiao lu xiao lu"
CONFIG_CUSTOM_WAKE_WORD_DISPLAY="小鹿小鹿"
CONFIG_CUSTOM_WAKE_WORD_THRESHOLD=20
CONFIG_SR_MN_CN_MULTINET7_QUANT=y
EOF

# 删除旧 sdkconfig，强制从 defaults 重新生成，确保 BT 所有依赖子项正确解析
rm -f sdkconfig
info "已删除旧 sdkconfig，将从 defaults 重新生成"

# ── 构建 ─────────────────────────────────────────────────────
info "开始构建（约 3-8 分钟）..."
idf.py \
  -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.ble" \
  -DBOARD_NAME="$VARIANT" -DBOARD_TYPE="$BOARD" \
  build

# ── 合并为单文件 ─────────────────────────────────────────────
info "合并 bin 文件..."
idf.py merge-bin

# ── 输出结果 ─────────────────────────────────────────────────
MERGED="build/merged-binary.bin"
SIZE=$(du -sh "$MERGED" 2>/dev/null | cut -f1)
echo ""
echo -e "${GREEN}╔══════════════════════════════════════════╗${NC}"
echo -e "${GREEN}║  构建成功！                               ║${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║  输出文件: ${MERGED}${NC}"
echo -e "${GREEN}║  文件大小: ${SIZE}${NC}"
echo -e "${GREEN}╠══════════════════════════════════════════╣${NC}"
echo -e "${GREEN}║  烧录命令（设备连接到 USB 后执行）:       ║${NC}"
echo -e "${GREEN}║  idf.py flash                            ║${NC}"
echo -e "${GREEN}║  或:                                     ║${NC}"
echo -e "${GREEN}║  esptool.py --chip esp32s3 \\             ║${NC}"
echo -e "${GREEN}║    --baud 921600 write_flash \\           ║${NC}"
echo -e "${GREEN}║    0x0 build/merged-binary.bin           ║${NC}"
echo -e "${GREEN}╚══════════════════════════════════════════╝${NC}"
echo ""
info "BLE 设备名将广播为 \"Xiaozhi-XXXX\"（MAC 后 4 位）"
info "浏览器扫描时选 namePrefix: 'Xiaozhi'"
info "写入 UUID: A0A0A0A1-1234-5678-9ABC-DEF012345678"

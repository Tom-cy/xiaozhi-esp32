# BLE 配网固件构建 & 烧录指南

> 板型：bread-compact-wifi（ESP32-S3）  
> 配网方式：微信小程序蓝牙配网  
> 唤醒词：小鹿小鹿

---

## 一、环境准备（只需做一次）

### 1. 安装 ESP-IDF v5.5.2

```bash
mkdir -p ~/esp && cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf && git checkout v5.5.2
git submodule update --init --recursive
./install.sh esp32s3
```

> 安装时间约 5-15 分钟，需要下载工具链和 Python 依赖。

### 2. 安装 cmake（如未安装）

```bash
brew install cmake
```

---

## 二、每次打开终端必须执行

```bash
source ~/esp/esp-idf/export.sh
```

> 关闭终端后环境会失效，下次使用前必须重新执行此命令。

**验证环境是否正常：**

```bash
idf.py --version
# 应输出：ESP-IDF v5.5.2
```

---

## 三、构建固件

```bash
cd /Users/micy/jxm/xiaozhi-esp32
./build_ble.sh
```

### 首次构建或配置有变更时，加 --clean 全量重建：

```bash
./build_ble.sh --clean
```

> 构建时间约 5-15 分钟。成功后输出文件：`build/merged-binary.bin`

---

## 四、烧录固件

设备通过 USB 连接电脑后执行：

```bash
# 查看串口号
ls /dev/cu.*

# 烧录
idf.py -p /dev/cu.usbserial-xxxx flash
```

或使用 esptool：

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbserial-xxxx \
  --baud 921600 write_flash 0x0 build/merged-binary.bin
```

---

## 五、查看日志（串口监控）

```bash
idf.py -p /dev/cu.usbserial-xxxx monitor
```

| 快捷键 | 功能 |
|--------|------|
| `Ctrl+]` | 退出监控 |
| `Ctrl+T` + `Ctrl+R` | 重启设备 |

> 波特率：115200

---

## 六、配网流程（小程序）

### 首次配网

1. 设备上电，无 WiFi 凭据，自动进入 BLE 配网模式
2. 屏幕显示"请打开管理后台，使用绑定设备功能"
3. 打开微信小程序 → 设备配网页面
4. 点击"开始配网"→ 扫描到 `Xiaozhi-XXXX`（XXXX 为 MAC 后 4 位）
5. 点击设备 → 连接
6. 选择或输入 WiFi 名称、密码、OTA 地址
7. 点击"发送配置"
8. 设备保存凭据后自动重启，连接 WiFi
9. 小程序轮询检测到设备上线 → 显示"绑定成功"

### 重新配网（更换 WiFi）

**方法一：长按 BOOT 按钮**（推荐）
- 设备任意状态下，长按 BOOT 按钮约 1-2 秒
- 设备进入 BLE 配网模式，重复上述配网步骤

**方法二：让设备连不上旧 WiFi**
- 关闭旧路由器，或把设备带到新环境
- 设备 60 秒连接超时后自动进入 BLE 配网模式

**方法三：清除 NVS（仅清 WiFi 凭据，固件不动）**

```bash
source ~/esp/esp-idf/export.sh
esptool.py --chip esp32s3 -p /dev/cu.usbserial-xxxx erase_region 0x9000 0x6000
```

---

## 七、按钮功能说明

| 按钮 | 短按 | 长按 |
|------|------|------|
| BOOT | 启动阶段→进入配网 / 正常状态→切换对话 | 任意时刻进入 BLE 配网模式 |
| RESET | 硬件复位 | — |
| VOLUME+ | 音量 +10 | 音量最大 |

---

## 八、常见问题

### idf.py 命令找不到

```bash
source ~/esp/esp-idf/export.sh
```

### Python 虚拟环境丢失

```bash
cd ~/esp/esp-idf
./install.sh esp32s3
source ~/esp/esp-idf/export.sh
```

### pip 权限错误（wheels 目录）

```bash
sudo chown -R micy /Users/micy/Library/Caches/pip
cd ~/esp/esp-idf && ./install.sh esp32s3
```

### cmake 找不到

```bash
brew install cmake
```

### 构建报 IDF 版本不符

项目要求 ESP-IDF >= 5.5.2，确认版本：

```bash
idf.py --version
```

如果是旧版本：

```bash
cd ~/esp/esp-idf
git checkout v5.5.2
git submodule update --init --recursive
./install.sh esp32s3
```

### 小程序扫描不到设备

- 确认设备已进入 BLE 配网模式（屏幕有提示）
- 确认手机蓝牙已开启
- 微信开发者工具不支持 BLE，必须用真机测试

### 小程序卡在"正在发送配置"

- 检查是否在真机上运行（非开发者工具）
- 靠近设备（1 米内）

### 配网后设备扫描不到

- 正常现象：设备联网后 BLE 停止广播
- 重新配网请长按 BOOT 按钮

---

## 九、固件关键配置

| 配置项 | 值 |
|--------|-----|
| 目标芯片 | ESP32-S3 |
| 配网方式 | BLE（`CONFIG_USE_SIMPLE_BLE_PROVISIONING=y`） |
| BLE 服务 UUID | `A0A0A0A0-1234-5678-9ABC-DEF012345678` |
| BLE 特征值 UUID | `A0A0A0A1-1234-5678-9ABC-DEF012345678` |
| 广播名称 | `Xiaozhi-XXXX`（MAC 后 4 位） |
| 唤醒词 | 小鹿小鹿（`xiao lu xiao lu`） |
| 唤醒词模型 | MultiNet7 中文量化版 |
| OTA 默认地址 | `https://tea.iskaola.com/xiaozhi/ota/` |
| 串口波特率 | 115200 |

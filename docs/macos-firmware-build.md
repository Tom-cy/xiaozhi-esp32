# macOS 固件构建与打包命令备忘

> 适用项目：`xiaozhi-esp32`  
> 目标板型：`bread-compact-wifi-ble`  
> 目标芯片：ESP32-S3  
> 输出固件：`build/merged-binary.bin`

本文只记录 macOS 下最常用的命令。完整配网、烧录和排障说明见 `docs/start.md`。

---

## 1. 首次安装 ESP-IDF 环境

只需要做一次。

```bash
mkdir -p ~/esp
cd ~/esp
git clone --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
git checkout v5.5.2
git submodule update --init --recursive
./install.sh esp32s3
```

如未安装 `cmake`：

```bash
brew install cmake
```

---

## 2. 每次打开终端先激活 ESP-IDF 环境

这是最容易忘的一步。`idf.py`、`esptool.py` 和 ESP-IDF 的 Python 虚拟环境都会通过这条命令注入到当前 shell。

```bash
source ~/esp/esp-idf/export.sh
```

验证：

```bash
idf.py --version
```

预期能看到类似：

```text
ESP-IDF v5.5.2
```

如果提示 `idf.py: command not found`，说明当前终端还没有执行 `source ~/esp/esp-idf/export.sh`。

---

## 3. 进入固件仓库

按实际本机路径进入仓库，例如：

```bash
cd /Users/micy/jxm/xiaozhi-esp32
```

如果仓库放在其他目录，替换成自己的路径即可。

---

## 4. 普通构建并打包固件

```bash
./build_ble.sh
```

脚本会自动完成：

1. 生成 `sdkconfig.defaults.ble`
2. 设置目标芯片 `esp32s3`
3. 构建 `bread-compact-wifi-ble`
4. 执行 `idf.py merge-bin`
5. 输出合并固件 `build/merged-binary.bin`

---

## 5. 全量清理后重新构建

首次构建、切换 ESP-IDF 版本、修改 Kconfig / sdkconfig 或遇到奇怪缓存问题时使用：

```bash
./build_ble.sh --clean
```

成功后重点确认输出文件存在：

```bash
ls -lh build/merged-binary.bin
```

---

## 6. 一条龙常用命令

每次打开新终端后，可以直接按下面顺序执行：

```bash
source ~/esp/esp-idf/export.sh
cd /Users/micy/jxm/xiaozhi-esp32
./build_ble.sh
ls -lh build/merged-binary.bin
```

需要全量重建时：

```bash
source ~/esp/esp-idf/export.sh
cd /Users/micy/jxm/xiaozhi-esp32
./build_ble.sh --clean
ls -lh build/merged-binary.bin
```

---

## 7. 烧录合并固件

先查看串口：

```bash
ls /dev/cu.*
```

使用 `idf.py` 烧录：

```bash
idf.py -p /dev/cu.usbserial-xxxx flash
```

或直接烧录合并后的单文件：

```bash
esptool.py --chip esp32s3 --port /dev/cu.usbserial-xxxx \
  --baud 921600 write_flash 0x0 build/merged-binary.bin
```

把 `/dev/cu.usbserial-xxxx` 替换成实际串口名。

---

## 8. 查看串口日志

```bash
idf.py -p /dev/cu.usbserial-xxxx monitor
```

常用快捷键：

| 快捷键 | 说明 |
|--------|------|
| `Ctrl+]` | 退出 monitor |
| `Ctrl+T` + `Ctrl+R` | 重启设备 |

---

## 9. 常见问题

### idf.py 找不到

```bash
source ~/esp/esp-idf/export.sh
```

### Python 虚拟环境或工具链损坏

```bash
cd ~/esp/esp-idf
./install.sh esp32s3
source ~/esp/esp-idf/export.sh
```

### ESP-IDF 版本不对

```bash
cd ~/esp/esp-idf
git checkout v5.5.2
git submodule update --init --recursive
./install.sh esp32s3
source ~/esp/esp-idf/export.sh
idf.py --version
```

### CMake 缓存导致配置没有生效

```bash
./build_ble.sh --clean
```

### pip 缓存权限错误

把用户名替换成当前 macOS 用户名：

```bash
sudo chown -R "$USER" ~/Library/Caches/pip
cd ~/esp/esp-idf
./install.sh esp32s3
source ~/esp/esp-idf/export.sh
```

---

## 10. 构建成功判断

看到以下文件即表示打包成功：

```text
build/merged-binary.bin
```

刷入后，设备启动日志应包含当前固件板型和 MQTT/BLE 相关初始化日志。对于 Java + EMQX 模式，联网后还应能看到：

```text
MQTT: Connected to endpoint
MQTT: Subscribed topic: devices/p2p/...
```

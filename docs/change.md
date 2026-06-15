# 变更记录

> 基准提交：`4a68924a98d6d8a903962ca1b7ed4e183d8876e9`（tea）  
> 统计截止：`98c9f99`（修复初始化问题）  
> 统计时间：2026-06-15

---

## 总览

| 指标 | 数值 |
|------|------|
| 提交次数 | 8 次 |
| 涉及文件 | 11 个 |
| 新增代码行 | 463 行 |
| 删除代码行 | 20 行 |
| 净增代码行 | 443 行 |
| 新增文件 | 3 个 |
| 修改文件 | 8 个 |

---

## 提交历史（时间正序）

| # | 提交 ID | 时间 | 作者 | 说明 | 变更文件 |
|---|---------|------|------|------|---------|
| 1 | `1b08aad` | 2026-06-15 16:51 | chenyuan | BLE 配网核心功能初始提交 | 10 个 |
| 2 | `b066719` | 2026-06-15 16:55 | chenyuan | 更新提示词配置 | 2 个 |
| 3 | `be78129` | 2026-06-15 18:32 | chenyuan | 启用 MultiNet7 语音识别模型 | 2 个 |
| 4 | `896db1c` | 2026-06-15 18:40 | chenyuan | NVS 写模式修复（Settings 构造参数） | 1 个 |
| 5 | `7e35daf` | 2026-06-15 19:01 | chenyuan | 补全 CMake 编译入口 + BLE 头文件完善 | 3 个 |
| 6 | `0868f12` | 2026-06-15 19:44 | chenyuan | BLE 头文件及构建脚本修复 | 2 个 |
| 7 | `ef5769d` | 2026-06-15 19:45 | chenyuan | Kconfig 尾部空行清理（打包成功版本） | 1 个 |
| 8 | `98c9f99` | 2026-06-15 20:42 | mi_cy | 修复 BLE 初始化崩溃导致的显示永久停留问题 | 3 个 |

---

## 文件变更明细

### 新增文件

#### `main/boards/common/ble_simple_prov.cpp` ＋205 行
轻量级 BLE GATT 配网服务实现。核心功能：
- 定义 GATT 服务 UUID（`A0A0A0A0-1234-5678-9ABC-DEF012345678`）和特征值 UUID（`A0A0A0A1-...`）
- 实现 `GattWriteCb`：接收前端通过蓝牙写入的 JSON 数据（`ssid`、`password`、`ota_url`）
- 实现 `StartAdvertising`：设备名以 `Xiaozhi-XXXX`（MAC 后 4 位）广播
- 实现 `BleSimpleProv::Start()`：初始化 NimBLE 栈并启动 FreeRTOS 宿主任务
- 提交 `98c9f99` 补充：`esp_nimble_init()` 返回值错误检查，防止未初始化栈引发 panic

#### `main/boards/common/ble_simple_prov.h` ＋64 行
BLE 配网服务头文件：
- 定义 `ProvCredentials` 结构体（ssid、password、ota_url）
- 定义 `ProvCallback` 回调类型
- 声明 `BleSimpleProv` 单例类（Start / Stop / HandleWrite）
- 暴露服务和特征值 UUID 宏，供前端 Web Bluetooth 对接

#### `build_ble.sh` ＋107 行
BLE 固件一键构建脚本：
- 目标芯片：ESP32-S3，目标板：`bread-compact-wifi`，变体：`bread-compact-wifi-ble`
- 自动写入 sdkconfig：禁用热点配网、启用 BLE 配网、启用自定义唤醒词（小鹿小鹿）、启用 MultiNet7 模型
- 支持 `--clean` 参数全量清理重建
- 最终输出 `build/merged-binary.bin` 合并烧录文件，并打印烧录命令

---

### 修改文件

#### `main/boards/common/wifi_board.cc` ＋38 / -0 行
WiFi 通用基类核心修改：
- **新增 BLE 分支**（`#elif CONFIG_USE_SIMPLE_BLE_PROVISIONING`）：在 `StartWifiConfigMode()` 中，当无热点/Blufi 配置时启用 BLE 配网
- **BLE 凭据回调**：收到 SSID/密码后写入 `SsidManager`，收到 OTA URL 后通过 `Settings("wifi", true)` 持久化到 NVS
- **提交 `98c9f99`**：在 BLE 启动前立即调用 `display->SetStatus()` 更新屏幕，确保崩溃时也能看到状态变化

#### `main/boards/bread-compact-wifi/config.json` ＋17 / -1 行
新增构建变体 `bread-compact-wifi-ble`，`sdkconfig_append` 配置项包括：
- `CONFIG_OLED_SSD1306_128X32=y`
- `CONFIG_USE_HOTSPOT_WIFI_PROVISIONING=n`
- `CONFIG_USE_SIMPLE_BLE_PROVISIONING=y`
- `CONFIG_BT_ENABLED=y` / `CONFIG_BT_NIMBLE_ENABLED=y` / `CONFIG_BT_NIMBLE_ROLE_PERIPHERAL=y`
- `CONFIG_USE_CUSTOM_WAKE_WORD=y`，唤醒词：`xiao lu xiao lu`（小鹿小鹿），阈值 20
- `CONFIG_SR_MN_CN_MULTINET7_QUANT=y`

#### `main/Kconfig.projbuild` ＋11 / -1 行
在 "WiFi Configuration Method" 菜单新增配网选项：
```
config USE_SIMPLE_BLE_PROVISIONING
    bool "Simple BLE (Web Bluetooth compatible)"
    select BT_ENABLED
    select BT_NIMBLE_ENABLED
    select BT_NIMBLE_ROLE_PERIPHERAL
```

#### `main/CMakeLists.txt` ＋3 / -0 行
新增条件编译入口：
```cmake
if (CONFIG_USE_SIMPLE_BLE_PROVISIONING)
    list(APPEND SOURCES "boards/common/ble_simple_prov.cpp")
endif()
```

#### `main/boards/bread-compact-esp32/esp32_bread_board.cc` ＋3 / -3 行
- 唤醒词从 `你好小智` 更改为 `小鹿小鹿`
- 空白字符规范化（去除多余空格）

#### `main/boards/bread-compact-esp32-lcd/esp32_bread_board_lcd.cc` ＋5 / -5 行
- 唤醒词从 `你好小智` 更改为 `小鹿小鹿`
- 空白字符规范化（去除多余空格/尾随空白）

#### `main/boards/esp32-cgc/esp32_cgc_board.cc` ＋6 / -6 行
- 唤醒词从 `你好小智` 更改为 `小鹿小鹿`
- 空白字符规范化

#### `main/boards/esp32-cgc-144/esp32_cgc_144_board.cc` ＋4 / -4 行
- 唤醒词从 `你好小智` 更改为 `小鹿小鹿`
- 空白字符规范化

---

## 功能变更总结

### 新增功能

**BLE 轻量配网（Web Bluetooth）**

前端（`hair-admins` 管理后台）通过 Web Bluetooth API，经蓝牙向设备写入 JSON：
```json
{"ssid":"...","password":"...","ota_url":"..."}
```
设备收到后自动保存凭据并触发 WiFi 连接，替代原有的热点配网方案。

- 服务 UUID：`A0A0A0A0-1234-5678-9ABC-DEF012345678`
- 特征值 UUID：`A0A0A0A1-1234-5678-9ABC-DEF012345678`（WRITE / WRITE_NO_RSP）
- 广播名称：`Xiaozhi-XXXX`（XXXX 为设备 MAC 后 4 位）
- 最大数据长度：512 字节

### 修改功能

**唤醒词更换**：`你好小智` → `小鹿小鹿`（影响 4 块板：bread-compact-esp32、bread-compact-esp32-lcd、esp32-cgc、esp32-cgc-144）

**NVS 写模式**：OTA URL 现通过 `Settings("wifi", true)` 以写模式持久化，之前使用默认只读模式

### 修复问题

**提交 `98c9f99`（mi_cy）**：修复 BLE 初始化崩溃导致设备永久显示"正在初始化"的问题
- `BleSimpleProv::Start()`：增加 `esp_nimble_init()` 返回值检查，失败时安全退出而非继续操作未初始化栈
- `WifiBoard::StartWifiConfigMode()`：在 BLE 启动前立即更新显示状态，避免崩溃时屏幕无任何提示

---

## 受影响的板型

| 板型 | 变更内容 |
|------|---------|
| bread-compact-wifi | 新增 BLE 配网变体，新增 config.json 构建配置 |
| bread-compact-esp32 | 唤醒词更换 |
| bread-compact-esp32-lcd | 唤醒词更换 |
| esp32-cgc | 唤醒词更换 |
| esp32-cgc-144 | 唤醒词更换 |

---

## BLE 配网接口说明

```
服务 UUID:      A0A0A0A0-1234-5678-9ABC-DEF012345678
特征值 UUID:    A0A0A0A1-1234-5678-9ABC-DEF012345678
属性:           WRITE | WRITE_NO_RSP
数据格式:       UTF-8 JSON，最大 512 字节

写入格式：
{
  "ssid":     "WiFi名称",
  "password": "WiFi密码",
  "ota_url":  "固件更新地址（可选）"
}
```

触发时机：设备无已保存 WiFi 时自动进入配网模式并开始 BLE 广播。

# 变更记录

## 关于本文件

本文件记录 xiaozhi-esp32 固件项目从基准版本开始的**所有代码变更**，包括 git 提交历史和未提交的调试修复过程。

**用途：**
- 快速了解项目做了哪些改动，改了哪些文件
- 排查问题时回溯历史变更，判断某个 bug 是哪次改动引入的
- 多人协作时同步修改背景，避免重复踩坑
- 记录调试过程中发现的根因和解决方案，供后续参考

**阅读指引：**
- 第一节「提交历史」对应 git log，每次提交一行
- 第二节「文件变更明细」说明每个文件改了什么、为什么改
- 第三节「调试修复记录」记录未提交的调试过程（根因分析 + 修复方案）
- 文件持续追加更新，最新变更在最后

---

> 基准提交：`4a68924a98d6d8a903962ca1b7ed4e183d8876e9`（tea）  
> 统计截止：`98c9f99`（修复初始化问题）  
> 统计时间：2026-06-15

---

## 调试修复记录（2026-06-15，未提交工作变更）

### 问题背景

串口日志显示 `BLE_INIT: hci inits failed` / `esp_nimble_init failed: ESP_FAIL`，设备 BLE 始终无法启动；前端「开始扫描」点击无反应。通过系统性分析串口日志与代码，定位到 3 个独立根因并逐一修复。

---

### 修复 1：`build_ble.sh` — sdkconfig 生成方式错误

**根因**：原脚本用 `cat >> sdkconfig` 直接追加 BLE 配置项到已有 sdkconfig，跳过了 Kconfig 依赖解析。`CONFIG_BT_ENABLED=y` 只是顶层开关，BT 控制器还需要数十个子配置项（HCI transport、控制器模式、WiFi+BLE 共存等），缺失这些项导致 `ble_controller_init()` 在 `esp_bt_controller_init()` 内部失败，报 `hci inits failed`。

**修复**：改用 `SDKCONFIG_DEFAULTS` 机制，由 ESP-IDF 构建系统在生成 sdkconfig 时自动解析所有 Kconfig 依赖。

变更内容：
- 新增 `sdkconfig.defaults.ble` 文件（脚本运行时生成），包含完整 BLE 所需配置
- 每次构建前自动删除旧 sdkconfig（`rm -f sdkconfig`），强制从 defaults 重新生成
- `idf.py build` 改为带 `-DSDKCONFIG_DEFAULTS` 参数调用

新增的关键配置项：

| 配置项 | 说明 |
|--------|------|
| `CONFIG_BT_CONTROLLER_ENABLED=y` | 显式启用 ESP32-S3 BT 控制器 |
| `CONFIG_BT_LE_ENABLED=y` | 显式启用 BLE（ESP32-S3 不支持 Classic BT） |
| `CONFIG_BT_CTRL_MODE_BLE_ONLY=y` | 控制器模式：仅 BLE |
| `CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y` | NimBLE 广播者角色（peripheral 依赖） |
| `CONFIG_ESP_COEX_ENABLED=y` | 启用 WiFi + BLE 共存 |
| `CONFIG_SW_COEXIST_ENABLE=y` | 软件共存调度（WiFi+BLE 同时工作必须） |

**影响文件**：`build_ble.sh`

---

### 修复 2：`deviceManager.vue` — BLE UUID 错误

**根因**：前端 BLE UUID 常量使用了 ESP-IDF 官方 `wifi_prov_mgr` 的标准 UUID，与固件自定义 UUID 完全不同，浏览器永远无法扫描到设备的 GATT 服务。

```typescript
// 修复前（错误）
const BLE_PROV_SERVICE = '021a9004-0382-4aea-bff4-6b3f1c5adfb4';  // wifi_prov_mgr UUID
const BLE_PROV_CONFIG  = '021aff52-0382-4aea-bff4-6b3f1c5adfb4';

// 修复后（正确，与 ble_simple_prov.h 一致）
const BLE_PROV_SERVICE = 'a0a0a0a0-1234-5678-9abc-def012345678';
const BLE_PROV_CONFIG  = 'a0a0a0a1-1234-5678-9abc-def012345678';
```

**影响文件**：`hair-admins/src/views/aiChat/deviceManager.vue`（第 414-415 行）

---

### 修复 3：`deviceManager.vue` — 扫描无反馈问题

**根因**：点击「开始扫描」无任何反应，有两个原因：
1. `bleSupported` 检测未包含 `isSecureContext` 检查——HTTP 页面下 `'bluetooth' in navigator` 可能为 true，但 `requestDevice()` 静默失败，按钮看似可点实则无效
2. `NotFoundError`（用户关闭蓝牙选择器 / 选择器内无设备）被完全静默吞掉，用户看不到任何提示

**修复**：

```typescript
// bleSupported 增加安全上下文检查
const bleSupported = ref(
    typeof navigator !== 'undefined' &&
    'bluetooth' in navigator &&
    (typeof window === 'undefined' || window.isSecureContext)
);

// NotFoundError 改为有意义的提示而非静默忽略
if (e?.name === 'NotFoundError') {
    ElMessage.warning('选择器里没有找到设备，请确认 ESP32 已进入配网模式后重试');
} else if (e?.name === 'SecurityError' || !window.isSecureContext) {
    ElMessage.error('Web Bluetooth 需要 HTTPS 页面，请通过 https:// 访问后台');
} else {
    ElMessage.error(`扫描失败：${e?.message || '未知错误'}`);
}
```

**影响文件**：`hair-admins/src/views/aiChat/deviceManager.vue`（`bleSupported` 定义、`startBleScan` 错误处理、模板警告文案）

---

### 变更文件汇总

| 文件 | 变更类型 | 说明 |
|------|----------|------|
| `build_ble.sh` | 修改 | sdkconfig 生成方式重构，补全 BLE 控制器及共存配置 |
| `hair-admins/src/views/aiChat/deviceManager.vue` | 修改 | BLE UUID 修正 + 扫描错误反馈修复 |

---

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

---

## 调试修复记录（2026-06-15，未提交工作变更）

### 问题背景

串口日志显示 `BLE_INIT: hci inits failed` / `esp_nimble_init failed: ESP_FAIL`，设备 BLE 始终无法启动；前端「开始扫描」点击无反应。通过系统性分析串口日志与代码，定位到 4 个独立根因并逐一修复。

---

### 修复 1：`build_ble.sh` — sdkconfig 生成方式错误（第一轮）

**根因**：原脚本用 `cat >> sdkconfig` 直接追加 BLE 配置项到已有 sdkconfig，跳过了 Kconfig 依赖解析。`CONFIG_BT_ENABLED=y` 只是顶层开关，BT 控制器还需要数十个子配置项（HCI transport、控制器模式、WiFi+BLE 共存等），缺失这些项导致 `ble_controller_init()` 在 `esp_bt_controller_init()` 内部失败，报 `hci inits failed`。

**第一轮修复**：改用 `SDKCONFIG_DEFAULTS` 机制，由 ESP-IDF 构建系统在生成 sdkconfig 时自动解析所有 Kconfig 依赖，并补充以下关键配置：

| 配置项 | 说明 |
|--------|------|
| `CONFIG_BT_CONTROLLER_ENABLED=y` | 显式启用 ESP32-S3 BT 控制器 |
| `CONFIG_BT_LE_ENABLED=y` | 显式启用 BLE（ESP32-S3 不支持 Classic BT） |
| `CONFIG_BT_CTRL_MODE_BLE_ONLY=y` | 控制器模式：仅 BLE |
| `CONFIG_BT_NIMBLE_ROLE_BROADCASTER=y` | NimBLE 广播者角色 |
| `CONFIG_ESP_COEX_ENABLED=y` | 启用 WiFi + BLE 共存 |
| `CONFIG_SW_COEXIST_ENABLE=y` | 软件共存调度（WiFi+BLE 同时工作必须） |

---

### 修复 1（真正根因）：`ble_simple_prov.cpp` — BT 控制器初始化顺序缺失

**根因**：通过对比同仓库 `blufi.cpp` 的正确实现，发现 `BleSimpleProv::Start()` 直接调用 `esp_nimble_init()`，漏掉了前置的两步控制器初始化。

```
正确顺序（blufi.cpp 参考）：
  1. esp_bt_controller_init(&bt_cfg)       ← 原代码缺失
  2. esp_bt_controller_enable(BLE 模式)    ← 原代码缺失
  3. esp_nimble_init()                      ← 原代码只有这步

esp_nimble_init() 内部调 esp_nimble_hci_init() 建立 VHCI 传输层
若控制器未 init + enable，VHCI 起不来 → 返回 ESP_FAIL
→ 串口报 E (1997) BLE_INIT: hci inits failed
```

**结论**：`build_ble.sh` 的 `CONFIG_BT_ENABLED` 等配置项本身没有问题，之前对 sdkconfig 和 CMakeCache 的所有修改均为误判方向，真正的问题纯粹是代码漏了两行。

**修复内容**（`main/boards/common/ble_simple_prov.cpp`）：

`Start()` 补齐三步初始化，用 `esp_bt_controller_get_status()` 防止重复初始化：
```cpp
// 1. 控制器 init
if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_IDLE) {
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
}
// 2. 控制器 enable
if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
    esp_bt_controller_enable(ESP_BT_MODE_BLE);
}
// 3. NimBLE 主机 init
esp_nimble_init();
```

`Stop()` 补齐对称清理，防止第二次进配网模式时 `esp_bt_controller_init()` 因控制器已存在返回 `ESP_ERR_INVALID_STATE`：
```cpp
nimble_port_stop();
esp_nimble_deinit();
esp_bt_controller_disable();
esp_bt_controller_deinit();
```

---

### 附：`build_ble.sh` sdkconfig 方式改进（非根因，属规范化改进）

原脚本 `cat >> sdkconfig` 直接追加配置跳过了 Kconfig 依赖解析，属于不规范做法，一并修正：

- 改用 `sdkconfig.defaults.ble` 文件 + `-DSDKCONFIG_DEFAULTS` 参数
- `set-target` 前先创建 defaults 文件并删除 `build/CMakeCache.txt`，防止旧缓存覆盖新参数
- 正确顺序：创建 defaults → 删缓存 → `set-target`（含 SDKCONFIG_DEFAULTS）→ `build`

---

### 修复 2：`deviceManager.vue` — BLE UUID 错误

**根因**：前端 BLE UUID 使用了 ESP-IDF 官方 `wifi_prov_mgr` 标准 UUID，与固件自定义 UUID 完全不同，浏览器无法扫描到正确的 GATT 服务。

```typescript
// 修复前（错误）
const BLE_PROV_SERVICE = '021a9004-0382-4aea-bff4-6b3f1c5adfb4';
const BLE_PROV_CONFIG  = '021aff52-0382-4aea-bff4-6b3f1c5adfb4';

// 修复后（与 ble_simple_prov.h 一致）
const BLE_PROV_SERVICE = 'a0a0a0a0-1234-5678-9abc-def012345678';
const BLE_PROV_CONFIG  = 'a0a0a0a1-1234-5678-9abc-def012345678';
```

**影响文件**：`hair-admins/src/views/aiChat/deviceManager.vue`

---

### 修复 3：`deviceManager.vue` — 扫描无反馈问题

**根因**：两个子问题：
1. `bleSupported` 未检查 `isSecureContext`，HTTP 页面下按钮看似可点但静默失败
2. `NotFoundError` 被完全吞掉，用户看不到任何提示

**修复**：`bleSupported` 增加 `isSecureContext` 检查；`NotFoundError` 改为友好提示；增加 `SecurityError` 处理并引导用户使用 HTTPS。

**影响文件**：`hair-admins/src/views/aiChat/deviceManager.vue`

---

### 修复 4：设备搜索不到 — 广播包缺少服务 UUID + 前端过滤器问题

**现象**：BLE 广播正常（串口日志确认 `BLE advertising as "Xiaozhi-27f4"`），但浏览器 picker 里找不到该设备。

**根因**：Chrome/Edge 执行 `requestDevice` 时，会把 `optionalServices` 里的 UUID 下发给操作系统作为 BLE 硬件扫描过滤器。固件广播包只包含设备名，没有服务 UUID，设备在 OS 层被过滤掉，名字过滤器根本来不及比对。

```
固件广播包（修复前）：[Flags] + [Complete Local Name: "Xiaozhi-27f4"]
  → Chrome 把 a0a0a0a0-... 传给 OS 做硬件过滤 → OS 没看到该 UUID → 设备被丢弃
  → picker 里永远看不到
```

**两处同步修复**：

固件（`main/boards/common/ble_simple_prov.cpp`）：在 `StartAdvertising()` 加入扫描响应包，携带服务 UUID：
```cpp
// 广播包放设备名，扫描响应包放服务 UUID（128-bit 太长放不进广播包）
struct ble_hs_adv_fields rsp_fields = {};
rsp_fields.uuids128 = (ble_uuid128_t*)&SVC_UUID;
rsp_fields.num_uuids128 = 1;
rsp_fields.uuids128_is_complete = 1;
ble_gap_adv_rsp_set_fields(&rsp_fields);
```

前端（`hair-admins/src/views/aiChat/deviceManager.vue`）：`requestDevice` filters 加入服务 UUID 条件（与名字前缀 OR 关系），让 Chrome 能按服务 UUID 匹配到扫描响应包里的数据：
```typescript
filters: [
    { services: [BLE_PROV_SERVICE] },   // 服务 UUID 过滤（主要，配合固件扫描响应）
    { namePrefix: 'Xiaozhi' },           // 名字前缀（备用）
    { namePrefix: 'PROV_' },
    { namePrefix: 'ESP' },
],
optionalServices: [BLE_PROV_SERVICE],
```

---

### 变更文件汇总

| 文件 | 说明 |
|------|------|
| `main/boards/common/ble_simple_prov.cpp` | **真正根因修复**：补齐 BT 控制器 init/enable，完善 Stop() 对称清理 |
| `build_ble.sh` | 规范化 sdkconfig 生成方式（非根因改进） |
| `hair-admins/src/views/aiChat/deviceManager.vue` | BLE UUID 修正 + 扫描错误反馈修复 |

---

### 修复 5：`ble_simple_prov.cpp` — 断开连接后不再重新广播

**现象**：固件开机后能正常广播 `Xiaozhi-27f4`（串口日志确认）；但只要任意 central（nRF Connect、浏览器 gatt.connect()、上一次配网中途取消）连上过一次，断开后设备永远停止广播，直到重启。浏览器 picker 自然空白，与过滤器无关。

**根因**：BLE 协议规定可连接模式（`BLE_GAP_CONN_MODE_UND`）广播在被 central 连接后立即停止。`ble_gap_adv_start` 原来第 5 个参数（GAP 事件回调）传的是 `nullptr`，断开事件没有任何处理，广播就此停止：

```
开机 → 广播开始
  → nRF Connect / 浏览器 gatt.connect() → 广播停止（协议要求）
  → 断开 → 没有回调 → 广播不恢复
  → 下次打开 picker → 扫描结果为空
```

**修复内容**（`main/boards/common/ble_simple_prov.cpp`）：

1. 新增 `s_keep_advertising` 标志（`Start()` 置 true，`Stop()` 置 false），防止配网成功后或拆栈期间误重启广播：
```cpp
static volatile bool s_keep_advertising = false;
```

2. 新增 GAP 事件回调 `GapEventCb`，处理三种场景下的重新广播：
```cpp
static int GapEventCb(struct ble_gap_event* event, void* arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            // 连接失败时恢复广播
            if (event->connect.status != 0 && s_keep_advertising) StartAdvertising();
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            // 连接断开后恢复广播 ← 关键修复
            if (s_keep_advertising) StartAdvertising();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            if (s_keep_advertising) StartAdvertising();
            break;
    }
    return 0;
}
```

3. `ble_gap_adv_start` 第 5 参数从 `nullptr` 改为 `GapEventCb`：
```cpp
// 修复前
ble_gap_adv_start(..., nullptr, nullptr);
// 修复后
ble_gap_adv_start(..., GapEventCb, nullptr);
```

4. `Start()` 末尾设置标志，`Stop()` 开头清除标志：
```cpp
// Start()
s_keep_advertising = true;

// Stop()
s_keep_advertising = false;  // 先关标志，防止拆栈期间回调误重启广播
```

**逻辑闭环**：
- 配网成功 → `HandleWrite` 触发 → `Stop()` → `s_keep_advertising = false` → 浏览器断开 BLE 不再重启广播
- 配网失败 / nRF Connect 连了又断 → `GapEventCb` → 自动重启广播 → 无需重启设备即可再次被扫到

**影响文件**：`main/boards/common/ble_simple_prov.cpp`

---

### 修复 6：`wifi_board.cc` — BLE 回调里调 Stop() 导致 WiFi 永不连接

**现象**：串口日志确认 BLE 成功收到 JSON 凭据（`Got SSID=CMCC-7KUE`）、NimBLE 已停止广播并断开连接，但此后设备只有 SystemInfo 心跳，永远不去连接 WiFi，不出现 `Starting WiFi connection attempt` 日志。

**根因**：BLE GATT 写入回调链最终在 NimBLE 宿主任务（`NimbleHostTask`）里执行。原回调代码直接调用 `BleSimpleProv::Stop()`，`Stop()` 内部依次调用：
```
nimble_port_stop()    ← 向当前运行的事件循环发信号要求退出
esp_nimble_deinit()   ← 在事件循环还未退出时就释放它的内部队列/内存
esp_bt_controller_disable()
esp_bt_controller_deinit()
```
在 NimBLE 事件循环内部释放自身资源属于不安全用法，导致 `nimble_port_run()` 退出后 `NimbleHostTask` 进入不确定状态。虽然 `Application::Schedule([this]() { OnNetworkEvent(WifiConfigModeExit); })` 已入队，但由于 NimBLE 任务状态异常，调度链未能执行，WiFi 连接流程永远不触发。

**修复**：凭据已写入 NVS 后直接调用 `esp_restart()`，而非尝试在运行中切换 BLE→WiFi 栈。`esp_restart()` 可从任意上下文（包括 NimBLE 回调）安全调用，重启后 `TryWifiConnect()` 读到已保存的 SSID 走正常连接流程。

```cpp
// 修复前
SsidManager::GetInstance().AddSsid(creds.ssid, creds.password);
BleSimpleProv::GetInstance().Stop();         // ← 在 NimBLE 任务里拆自身，不安全
in_config_mode_ = false;
Application::GetInstance().Schedule([this]() {
    OnNetworkEvent(NetworkEvent::WifiConfigModeExit);  // ← 不执行
});

// 修复后
SsidManager::GetInstance().AddSsid(creds.ssid, creds.password);
ESP_LOGI("WifiBoard", "WiFi credentials saved, restarting to connect...");
esp_restart();   // ← 凭据已落盘，重启即可
```

同时新增 `#include <esp_system.h>` 头文件引用（`esp_restart()` 的声明）。

**影响文件**：`main/boards/common/wifi_board.cc`

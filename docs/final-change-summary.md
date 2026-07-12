# xiaozhi-esp32 最终修改汇总

本文按文件维度汇总当前 `xiaozhi-esp32` 定制版本的最终修改结果，用于快速了解每个文件最终承担的改动职责。单次排查过程和根因分析仍以 `docs/change.md` 为准。

## 总览

本定制版本围绕四条主线修改：

1. BLE 配网：新增轻量 BLE GATT 配网服务，支持 Web Bluetooth / 小程序写入 WiFi 和 OTA URL。
2. BLE 稳定性：修复 BLE 控制器初始化、断连后重新广播、配网成功后 WiFi 不连接等问题。
3. 设备身份：新增 BLE READ 特征读取真实 MAC，解决 iOS 返回随机 UUID 导致绑定错设备的问题。
4. MQTT/UDP 对话：补齐标准 MQTT Broker 模式下的 `subscribe_topic` 订阅能力，使 Java + EMQX 下行消息能投递到设备。

---

## 固件源码文件

### `main/boards/common/ble_simple_prov.cpp`

最终职责：实现 BLE GATT 配网服务。

主要修改：

- 新增自定义 BLE 服务 `A0A0A0A0-1234-5678-9ABC-DEF012345678`。
- 新增 WiFi 配置 WRITE 特征 `A0A0A0A1-1234-5678-9ABC-DEF012345678`，接收 JSON：

```json
{"ssid":"...","password":"...","ota_url":"..."}
```

- 新增 MAC READ 特征 `A0A0A0A2-1234-5678-9ABC-DEF012345678`，返回设备真实 MAC。
- BLE 广播名使用 `Xiaozhi-XXXX`，其中 `XXXX` 为 MAC 后四位。
- 广播响应包携带服务 UUID，提升 Web Bluetooth / 小程序扫描命中率。
- 补齐 BT Controller `init` / `enable` / NimBLE `init` 初始化顺序。
- 补齐 `Stop()` 中 NimBLE 和 BT Controller 对称清理。
- 增加 GAP 事件回调，连接断开或广告结束后自动恢复广播。
- 使用 `s_keep_advertising` 防止配网成功或拆栈阶段误重启广播。

### `main/boards/common/ble_simple_prov.h`

最终职责：声明 BLE 配网服务接口。

主要修改：

- 新增 `ProvCredentials`，承载 `ssid`、`password`、`ota_url`。
- 新增 `ProvCallback`，用于将 BLE 收到的配网数据交给网络层。
- 声明 `BleSimpleProv` 单例和 `Start` / `Stop` / `HandleWrite` 等接口。
- 暴露自定义 BLE 服务和特征 UUID，方便前端或小程序对接。

### `main/boards/common/wifi_board.cc`

最终职责：在 WiFi 配置模式中接入 BLE 配网。

主要修改：

- 在 `StartWifiConfigMode()` 中新增 `CONFIG_USE_SIMPLE_BLE_PROVISIONING` 分支。
- 启动 BLE 配网前更新屏幕状态，避免初始化异常时显示停留在旧状态。
- BLE 收到 SSID / 密码后写入 `SsidManager`。
- BLE 收到 OTA URL 后通过 `Settings("wifi", true)` 持久化到 NVS。
- 配网凭据落盘后调用 `esp_restart()`，重启后由正常 WiFi 连接流程读取凭据并联网。
- 新增 `esp_system.h` 引用以使用 `esp_restart()`。

### `main/boards/bread-compact-wifi/config.json`

最终职责：新增 BLE 配网构建变体。

主要修改：

- 新增 `bread-compact-wifi-ble` 变体。
- 禁用热点配网，启用 `CONFIG_USE_SIMPLE_BLE_PROVISIONING`。
- 启用 BT / NimBLE / NimBLE peripheral。
- 启用自定义唤醒词 `xiao lu xiao lu`。
- 启用 MultiNet7 中文模型。

### `main/Kconfig.projbuild`

最终职责：暴露 BLE 配网构建开关。

主要修改：

- 新增 `USE_SIMPLE_BLE_PROVISIONING` 选项。
- 自动选择 `BT_ENABLED`、`BT_NIMBLE_ENABLED`、`BT_NIMBLE_ROLE_PERIPHERAL`。

### `main/CMakeLists.txt`

最终职责：按配置编译 BLE 配网源码。

主要修改：

- 当 `CONFIG_USE_SIMPLE_BLE_PROVISIONING` 开启时，将 `boards/common/ble_simple_prov.cpp` 加入编译。

### `main/protocols/mqtt_protocol.h`

最终职责：MQTT/UDP 协议状态声明。

主要修改：

- 保留 `publish_topic_` 用于设备上行消息。
- 新增 `subscribe_topic_` 用于服务端下行消息。

### `main/protocols/mqtt_protocol.cc`

最终职责：实现 MQTT hello / 控制消息收发与 UDP 音频通道。

主要修改：

- 从 MQTT 设置读取 `publish_topic`。
- 新增读取 `subscribe_topic`。
- MQTT 连接成功后主动订阅 `subscribe_topic`。
- 订阅失败时设置服务端错误并终止连接流程。
- 收到 MQTT 下行 JSON 后继续按原逻辑分发：
  - `hello` -> `ParseServerHello`
  - `goodbye` -> 关闭音频通道
  - 其他 JSON -> `on_incoming_json_`

这项修改是 Java + EMQX 标准 Broker 模式下接收 hello reply 的关键条件。

### `main/application.cc`

2026-07-12 补充最终状态：
- 新增 listening turn 状态机，避免设备在 MQTT/UDP 通道已经建立后一直红灯停留在聆听中。
- 当 `kListeningModeAutoStop` 下没有检测到过人声时，进入 listening 后 5 秒自动 `StopListening(no_speech_timeout)`。
- 当已经检测到过人声时，最后一次人声后静音 1.2 秒自动 `StopListening(vad_silence)`。
- listening 超过 15 秒会自动 `StopListening(max_listening_timeout)`。
- `listen stop` 会携带 `reason` 字段，便于 Java 日志和后续问题定位。

最终职责：应用主状态机、唤醒词、聆听、说话和音频通道编排。

主要修改：

- BLE 配网流程中配合 `wifi_board.cc` 进入网络激活和协议初始化。
- MQTT/UDP 模式下，唤醒词触发后打开音频通道并进入 `listening`。
- 修复 VAD 静音后没有自动停止聆听的问题：在 `kListeningModeAutoStop` 且 `IsVoiceDetected() == false` 时触发 `StopListening()`。
- 修复后设备会在用户说完话后发送 MQTT `listen stop`，Java 网关才能开始 ASR / AI / TTS。

### `main/boards/bread-compact-esp32/esp32_bread_board.cc`

最终职责：面包板 ESP32 版本板级配置。

主要修改：

- 唤醒词从默认小智唤醒词改为“小鹿小鹿”。
- 清理部分空白格式。

### `main/boards/bread-compact-esp32-lcd/esp32_bread_board_lcd.cc`

最终职责：面包板 ESP32 LCD 版本板级配置。

主要修改：

- 唤醒词改为“小鹿小鹿”。
- 清理部分空白格式。

### `main/boards/esp32-cgc/esp32_cgc_board.cc`

最终职责：ESP32 CGC 板级配置。

主要修改：

- 唤醒词改为“小鹿小鹿”。
- 清理部分空白格式。

### `main/boards/esp32-cgc-144/esp32_cgc_144_board.cc`

最终职责：ESP32 CGC 1.44 板级配置。

主要修改：

- 唤醒词改为“小鹿小鹿”。
- 清理部分空白格式。

---

## 构建脚本

### `build_ble.sh`

最终职责：构建 BLE 配网固件变体。

主要修改：

- 面向 `bread-compact-wifi-ble` 变体构建固件。
- 生成并使用 `sdkconfig.defaults.ble`。
- 通过 `SDKCONFIG_DEFAULTS` 交给 ESP-IDF 解析 Kconfig 依赖。
- 支持 `--clean` 全量清理重建。
- 输出合并烧录文件 `build/merged-binary.bin`。

---

## 文档文件

### `docs/mqtt-udp.md`

最终职责：英文 MQTT/UDP 协议说明。

主要修改：

- 补充 `subscribe_topic` 配置项，说明其用于接收服务端到设备的下行消息。

### `docs/mqtt-udp_zh.md`

最终职责：中文 MQTT/UDP 协议说明。

主要修改：

- 补充 `subscribe_topic` 配置项，说明其用于接收服务端下行消息。

### `docs/change.md`

2026-07-12 补充最终状态：
- 新增“修复 10：listening 静音兜底超时，修复红灯一直聆听中”。
- 新增“修复 11：升级为 listening turn 状态机并让 `listen stop` 携带 reason”。
- 记录本次 Java 已收到 `listen detect/start` 但 ESP32 没有发送 `listen stop` 的排查结论、WebSocket/MQTT 架构差异和双保险修复方案。

最终职责：按时间记录完整变更历史和排查过程。

主要修改：

- 持续记录 BLE 配网、构建、扫描、MAC READ、MQTT 下行订阅等问题的背景、根因和修复方案。
- 2026-07-11 新增 MQTT 标准 Broker 模式下订阅 `subscribe_topic` 的修复记录。

### `docs/final-change-summary.md`

最终职责：按文件维度汇总最终修改状态。

主要修改：

- 新增本文档，避免只从单次问题记录中理解当前代码。
- 汇总所有主要源码、构建脚本和文档文件的最终改动结果。

### `docs/macos-firmware-build.md`

最终职责：记录 macOS 下构建、打包、烧录 BLE 固件的常用命令。

主要修改：

- 记录 ESP-IDF v5.5.2 首次安装命令。
- 记录每次打开终端需要执行的 `source ~/esp/esp-idf/export.sh`。
- 记录 `./build_ble.sh`、`./build_ble.sh --clean`、`idf.py flash`、`esptool.py write_flash` 等常用命令。
- 记录 `idf.py` 找不到、Python 虚拟环境损坏、CMake 缓存等常见问题处理方式。

---

## 外部配套修改记录

以下文件不属于 `xiaozhi-esp32` 固件仓库，但与本固件定制强相关，已在 `docs/change.md` 中作为配套变更记录：

### `hair-admins/src/views/aiChat/deviceManager.vue`

主要修改：

- BLE 服务 UUID / 特征 UUID 改为固件自定义 UUID。
- 增加 `isSecureContext` 检查，提示 Web Bluetooth 必须在 HTTPS 安全上下文使用。
- `NotFoundError` / `SecurityError` 给出用户可理解的提示。
- 扫描过滤器加入服务 UUID，提高设备发现成功率。

### `uniapp-operation/pages/tab/deviceBind.vue`

主要修改：

- 小程序 BLE 配网读取 `A0A0A0A2` MAC READ 特征。
- iOS 使用固件返回的真实 MAC 绑定设备，而不是 CoreBluetooth 随机 UUID。
- Android 兼容旧路径：`deviceId` 符合 MAC 正则时可直接使用。
- 绑定流程不再单纯依赖设备在线轮询结果。

---

## 当前 MQTT/UDP 最终流程

```text
1. 小程序 / Web 通过 BLE 写入 WiFi 与 OTA URL
2. 设备重启并连接 WiFi
3. 设备请求 OTA 地址
4. OTA 返回 MQTT 配置：
   - endpoint
   - client_id
   - username
   - password
   - publish_topic
   - subscribe_topic
5. ESP32 连接 MQTT Broker
6. ESP32 订阅 subscribe_topic
7. 唤醒后 ESP32 发布 hello 到 publish_topic
8. Java 网关从 device-server/# 收到 hello
9. Java 网关发布 hello reply 到 devices/p2p/{deviceKey}
10. EMQX 根据 ESP32 的订阅把 hello reply 投递给设备
11. ESP32 解析 hello reply，建立 UDP 音频通道
```

---

## 关键验证点

刷入当前固件后，串口应看到：

```text
MQTT: Connected to endpoint
MQTT: Subscribed topic: devices/p2p/D8_85_AC_A2_27_F4
```

EMQX 应看到设备订阅授权：

```text
clientid=esp32_D8_85_AC_A2_27_F4 topic=devices/p2p/D8_85_AC_A2_27_F4
```

Java 网关应看到：

```text
[XiaozhiMQTT] received topic=device-server/D8_85_AC_A2_27_F4 type=hello
[XiaozhiHello] reply deviceId=D8:85:AC:A2:27:F4
[XiaozhiMQTT] publish topic=devices/p2p/D8_85_AC_A2_27_F4
```

设备不应再停在：

```text
MQTT: Failed to receive server hello
```

#pragma once

#include "sdkconfig.h"

#ifdef CONFIG_USE_SIMPLE_BLE_PROVISIONING

#include <string>
#include <functional>

/**
 * 轻量 BLE GATT 配网服务
 *
 * 浏览器通过 Web Bluetooth API 直接写入 JSON：
 *   {"ssid":"...","password":"...","ota_url":"..."}
 *
 * 无加密握手，兼容所有支持 Web Bluetooth 的 Chrome/Edge 浏览器。
 *
 * 服务 UUID:      A0A0A0A0-1234-5678-9ABC-DEF012345678
 * 特征值 UUID:    A0A0A0A1-1234-5678-9ABC-DEF012345678
 *   - 属性: WRITE | WRITE_NO_RSP
 *   - 格式: UTF-8 JSON，最大 512 字节
 */

// 浏览器写入到这两个 UUID
#define SIMPLE_PROV_SVC_UUID  "A0A0A0A0-1234-5678-9ABC-DEF012345678"
#define SIMPLE_PROV_CFG_UUID  "A0A0A0A1-1234-5678-9ABC-DEF012345678"

struct ProvCredentials {
    std::string ssid;
    std::string password;
    std::string ota_url;
};

using ProvCallback = std::function<void(const ProvCredentials&)>;

class BleSimpleProv {
public:
    static BleSimpleProv& GetInstance();

    /**
     * 初始化 BLE，开始广播。
     * @param on_received  收到完整凭据后的回调（在 BLE task 上下文执行）
     */
    void Start(ProvCallback on_received);

    /** 停止 BLE，释放资源 */
    void Stop();

    BleSimpleProv(const BleSimpleProv&) = delete;
    BleSimpleProv& operator=(const BleSimpleProv&) = delete;

private:
    BleSimpleProv() = default;
    ~BleSimpleProv() = default;

    ProvCallback callback_;
    bool started_ = false;

    void HandleWrite(const uint8_t* data, size_t len);
    static void NimbleHostTask(void* param);
};

#endif // CONFIG_USE_SIMPLE_BLE_PROVISIONING

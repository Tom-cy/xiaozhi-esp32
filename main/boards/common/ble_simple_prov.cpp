#include "ble_simple_prov.h"

#ifdef CONFIG_USE_SIMPLE_BLE_PROVISIONING

#include <cstring>
#include <string>
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "system_info.h"   // SystemInfo::GetMacAddress()
#include "cJSON.h"

static const char* TAG = "BleSimpleProv";

// ── UUID 定义（128-bit，小端序） ───────────────────────────────
// A0A0A0A0-1234-5678-9ABC-DEF012345678
static const ble_uuid128_t SVC_UUID = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = {
        0x78, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A,
        0x78, 0x56, 0x34, 0x12, 0xA0, 0xA0, 0xA0, 0xA0,
    },
};

// A0A0A0A1-1234-5678-9ABC-DEF012345678
static const ble_uuid128_t CFG_UUID = {
    .u = { .type = BLE_UUID_TYPE_128 },
    .value = {
        0x78, 0x56, 0x34, 0x12, 0xF0, 0xDE, 0xBC, 0x9A,
        0x78, 0x56, 0x34, 0x12, 0xA1, 0xA0, 0xA0, 0xA0,
    },
};

// ── 单例访问 ───────────────────────────────────────────────────
BleSimpleProv& BleSimpleProv::GetInstance() {
    static BleSimpleProv instance;
    return instance;
}

// ── GATT 写入回调 ───────────────────────────────────────────────
static int GattWriteCb(uint16_t conn_handle, uint16_t attr_handle,
                       struct ble_gatt_access_ctxt* ctxt, void* arg) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    // 读取写入数据
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    if (len == 0 || len > 512) {
        ESP_LOGW(TAG, "Write length out of range: %d", len);
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    char buf[513] = {};
    os_mbuf_copydata(ctxt->om, 0, len, buf);
    buf[len] = '\0';
    ESP_LOGI(TAG, "Received: %s", buf);

    BleSimpleProv::GetInstance().HandleWrite(
        reinterpret_cast<const uint8_t*>(buf), len);
    return 0;
}

void BleSimpleProv::HandleWrite(const uint8_t* data, size_t len) {
    cJSON* root = cJSON_ParseWithLength(reinterpret_cast<const char*>(data), len);
    if (!root) {
        ESP_LOGW(TAG, "JSON parse failed");
        return;
    }

    ProvCredentials creds;
    auto getStr = [&](const char* key) -> std::string {
        cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
        return (cJSON_IsString(item) && item->valuestring) ? item->valuestring : "";
    };

    creds.ssid     = getStr("ssid");
    creds.password = getStr("password");
    creds.ota_url  = getStr("ota_url");
    cJSON_Delete(root);

    if (creds.ssid.empty()) {
        ESP_LOGW(TAG, "ssid missing in JSON");
        return;
    }

    ESP_LOGI(TAG, "Got SSID=%s ota_url=%s", creds.ssid.c_str(), creds.ota_url.c_str());

    if (callback_) {
        callback_(creds);
    }
}

// ── GATT 服务定义 ───────────────────────────────────────────────
static const struct ble_gatt_svc_def GATT_SVCS[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &SVC_UUID.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &CFG_UUID.u,
                .access_cb = GattWriteCb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            { 0 },   // terminator
        },
    },
    { 0 },   // terminator
};

// ── GAP 广播 ───────────────────────────────────────────────────
static void StartAdvertising() {
    // 构建设备名：Xiaozhi-XXXX（MAC 后 4 位）
    std::string mac = SystemInfo::GetMacAddress();
    // MAC 格式 AA:BB:CC:DD:EE:FF → 取最后 4 个 hex 字符 (EE+FF)
    std::string suffix;
    for (char c : mac) {
        if (c != ':') suffix += c;
    }
    if (suffix.size() > 4) suffix = suffix.substr(suffix.size() - 4);
    std::string dev_name = "Xiaozhi-" + suffix;

    ble_svc_gap_device_name_set(dev_name.c_str());

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<const uint8_t*>(dev_name.c_str());
    fields.name_len = dev_name.size();
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);
    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER,
                               &adv_params, nullptr, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising as \"%s\"", dev_name.c_str());
    }
}

// ── NimBLE 主机同步回调 ─────────────────────────────────────────
static void OnSync() {
    ble_hs_util_ensure_addr(0);
    StartAdvertising();
}

static void OnReset(int reason) {
    ESP_LOGW(TAG, "NimBLE reset: reason=%d", reason);
}

// ── NimBLE 宿主任务 ────────────────────────────────────────────
void BleSimpleProv::NimbleHostTask(void* param) {
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(nullptr);
}

// ── 公共接口 ───────────────────────────────────────────────────
void BleSimpleProv::Start(ProvCallback on_received) {
    if (started_) return;
    started_  = true;
    callback_ = std::move(on_received);

    esp_nimble_init();

    ble_hs_cfg.sync_cb  = OnSync;
    ble_hs_cfg.reset_cb = OnReset;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(GATT_SVCS);
    ble_gatts_add_svcs(GATT_SVCS);

    nimble_port_freertos_init(NimbleHostTask);
    ESP_LOGI(TAG, "BleSimpleProv started");
}

void BleSimpleProv::Stop() {
    if (!started_) return;
    started_ = false;
    nimble_port_stop();
    ESP_LOGI(TAG, "BleSimpleProv stopped");
}

#endif // CONFIG_USE_SIMPLE_BLE_PROVISIONING

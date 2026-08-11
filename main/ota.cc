#include "ota.h"
#include "system_info.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <esp_app_format.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <esp_heap_caps.h>
#ifdef SOC_HMAC_SUPPORTED
#include <esp_hmac.h>
#endif

#include <cstring>
#include <vector>
#include <sstream>
#include <algorithm>
#include <cstdlib>

#define TAG "Ota"


Ota::Ota() {
#ifdef ESP_EFUSE_BLOCK_USR_DATA
    // Read Serial Number from efuse user_data
    uint8_t serial_number[33] = {0};
    if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, serial_number, 32 * 8) == ESP_OK) {
        if (serial_number[0] == 0) {
            has_serial_number_ = false;
        } else {
            serial_number_ = std::string(reinterpret_cast<char*>(serial_number), 32);
            has_serial_number_ = true;
        }
    }
#endif
}

Ota::~Ota() {
}

std::string Ota::GetCheckVersionUrl() {
    Settings settings("wifi", false);
    std::string url = settings.GetString("ota_url");
    if (url.empty()) {
        url = CONFIG_OTA_URL;
    }
    return url;
}

std::unique_ptr<Http> Ota::SetupHttp() {
    auto& board = Board::GetInstance();
    auto network = board.GetNetwork();
    auto http = network->CreateHttp(0);
    auto user_agent = SystemInfo::GetUserAgent();
    http->SetHeader("Activation-Version", has_serial_number_ ? "2" : "1");
    http->SetHeader("Device-Id", SystemInfo::GetMacAddress().c_str());
    http->SetHeader("Client-Id", board.GetUuid());
    if (has_serial_number_) {
        http->SetHeader("Serial-Number", serial_number_.c_str());
        ESP_LOGI(TAG, "Setup HTTP, User-Agent: %s, Serial-Number: %s", user_agent.c_str(), serial_number_.c_str());
    }
    http->SetHeader("User-Agent", user_agent);
    http->SetHeader("Accept-Language", Lang::CODE);
    http->SetHeader("Content-Type", "application/json");

    return http;
}

/*
 * Specification: https://ccnphfhqs21z.feishu.cn/wiki/FjW6wZmisimNBBkov6OcmfvknVd
 */
esp_err_t Ota::CheckVersion() {
    auto& board = Board::GetInstance();
    auto app_desc = esp_app_get_description();

    // Check if there is a new firmware version available
    current_version_ = app_desc->version;
    ESP_LOGI(TAG, "Current version: %s", current_version_.c_str());

    std::string url = GetCheckVersionUrl();
    if (url.length() < 10) {
        ESP_LOGE(TAG, "Check version URL is not properly set");
        return ESP_ERR_INVALID_ARG;
    }

    auto http = SetupHttp();

    std::string data = board.GetSystemInfoJson();
    std::string method = data.length() > 0 ? "POST" : "GET";
    http->SetContent(std::move(data));

    if (!http->Open(method, url)) {
        int last_error = http->GetLastError();
        ESP_LOGE(TAG, "Failed to open HTTP connection, code=0x%x", last_error);
        return last_error;
    }

    auto status_code = http->GetStatusCode();
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to check version, status code: %d", status_code);
        return status_code;
    }

    data = http->ReadAll();
    http->Close();

    // Response: { "firmware": { "version": "1.0.0", "url": "http://" } }
    // Parse the JSON response and check if the version is newer
    // If it is, set has_new_version_ to true and store the new version and URL

    cJSON *root = cJSON_Parse(data.c_str());
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    has_activation_code_ = false;
    has_activation_challenge_ = false;
    cJSON *activation = cJSON_GetObjectItem(root, "activation");
    if (cJSON_IsObject(activation)) {
        cJSON* message = cJSON_GetObjectItem(activation, "message");
        if (cJSON_IsString(message)) {
            activation_message_ = message->valuestring;
        }
        cJSON* code = cJSON_GetObjectItem(activation, "code");
        if (cJSON_IsString(code)) {
            activation_code_ = code->valuestring;
            has_activation_code_ = true;
        }
        cJSON* challenge = cJSON_GetObjectItem(activation, "challenge");
        if (cJSON_IsString(challenge)) {
            activation_challenge_ = challenge->valuestring;
            has_activation_challenge_ = true;
        }
        cJSON* timeout_ms = cJSON_GetObjectItem(activation, "timeout_ms");
        if (cJSON_IsNumber(timeout_ms)) {
            activation_timeout_ms_ = timeout_ms->valueint;
        }
    }

    has_mqtt_config_ = false;
    cJSON *mqtt = cJSON_GetObjectItem(root, "mqtt");
    if (cJSON_IsObject(mqtt)) {
        Settings settings("mqtt", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, mqtt) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_mqtt_config_ = true;
    } else {
        ESP_LOGI(TAG, "No mqtt section found !");
    }

    has_websocket_config_ = false;
    cJSON *websocket = cJSON_GetObjectItem(root, "websocket");
    if (cJSON_IsObject(websocket)) {
        Settings settings("websocket", true);
        cJSON *item = NULL;
        cJSON_ArrayForEach(item, websocket) {
            if (cJSON_IsString(item)) {
                if (settings.GetString(item->string) != item->valuestring) {
                    settings.SetString(item->string, item->valuestring);
                }
            } else if (cJSON_IsNumber(item)) {
                if (settings.GetInt(item->string) != item->valueint) {
                    settings.SetInt(item->string, item->valueint);
                }
            }
        }
        has_websocket_config_ = true;
    } else {
        ESP_LOGI(TAG, "No websocket section found!");
    }

    has_server_time_ = false;
    cJSON *server_time = cJSON_GetObjectItem(root, "server_time");
    if (cJSON_IsObject(server_time)) {
        cJSON *timestamp = cJSON_GetObjectItem(server_time, "timestamp");
        cJSON *timezone_offset = cJSON_GetObjectItem(server_time, "timezone_offset");

        if (cJSON_IsNumber(timestamp)) {
            // 设置系统时间
            struct timeval tv;
            double ts = timestamp->valuedouble;

            // 如果有时区偏移，计算本地时间
            if (cJSON_IsNumber(timezone_offset)) {
                ts += (timezone_offset->valueint * 60 * 1000); // 转换分钟为毫秒
            }

            tv.tv_sec = (time_t)(ts / 1000);  // 转换毫秒为秒
            tv.tv_usec = (suseconds_t)((long long)ts % 1000) * 1000;  // 剩余的毫秒转换为微秒
            settimeofday(&tv, NULL);
            has_server_time_ = true;
        }
    } else {
        ESP_LOGW(TAG, "No server_time section found!");
    }

    has_new_version_ = false;
    cJSON *firmware = cJSON_GetObjectItem(root, "firmware");
    if (cJSON_IsObject(firmware)) {
        cJSON *version = cJSON_GetObjectItem(firmware, "version");
        if (cJSON_IsString(version)) {
            firmware_version_ = version->valuestring;
        }
        cJSON *url = cJSON_GetObjectItem(firmware, "url");
        if (cJSON_IsString(url)) {
            firmware_url_ = url->valuestring;
        }

        if (cJSON_IsString(version) && cJSON_IsString(url)) {
            // Check if the version is newer, for example, 0.1.0 is newer than 0.0.1
            has_new_version_ = IsNewVersionAvailable(current_version_, firmware_version_);
            if (has_new_version_) {
                ESP_LOGI(TAG, "New version available: %s", firmware_version_.c_str());
            } else {
                ESP_LOGI(TAG, "Current is the latest version");
            }
            // If the force flag is set to 1, the given version is forced to be installed
            cJSON *force = cJSON_GetObjectItem(firmware, "force");
            if (cJSON_IsNumber(force) && force->valueint == 1) {
                has_new_version_ = true;
            }
        }
    } else {
        ESP_LOGW(TAG, "No firmware section found!");
    }

    cJSON_Delete(root);
    return ESP_OK;
}

void Ota::MarkCurrentVersionValid() {
    auto partition = esp_ota_get_running_partition();
    if (strcmp(partition->label, "factory") == 0) {
        ESP_LOGI(TAG, "Running from factory partition, skipping");
        return;
    }

    ESP_LOGI(TAG, "Running partition: %s", partition->label);
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(partition, &state) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get state of partition");
        return;
    }

    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "Marking firmware as valid");
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to mark firmware as valid: %s", esp_err_to_name(err));
        }
    }
}

bool Ota::Upgrade(const std::string& firmware_url,
                  std::function<void(int progress, size_t speed)> callback,
                  std::function<void(const OtaStatus& status)> status_callback) {
    ESP_LOGI(TAG, "Upgrading firmware from %s", firmware_url.c_str());
    constexpr int kHttpTimeoutMs = 90000;
    constexpr int kMaxRetries = 3;
    constexpr size_t kPageSize = 4096;

    esp_ota_handle_t update_handle = 0;
    auto update_partition = esp_ota_get_next_update_partition(NULL);
    if (update_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get update partition");
        return false;
    }

    ESP_LOGI(TAG, "Writing to partition %s at offset 0x%lx", update_partition->label, update_partition->address);
    bool ota_started = false;
    bool image_header_checked = false;
    std::string image_header;
    char* buffer = (char*)heap_caps_malloc(kPageSize, MALLOC_CAP_INTERNAL);
    if (buffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return false;
    }

    size_t buffer_offset = 0;
    size_t total_read = 0;
    size_t total_length = 0;
    size_t recent_read = 0;
    int retry_count = 0;
    int last_reported_progress = -5;
    std::string final_error_code;
    std::string final_error_message;

    auto report = [&](const std::string& state, const std::string& error_code = "",
                      const std::string& error_message = "") {
        if (!status_callback) return;
        OtaStatus status;
        status.state = state;
        status.progress = total_length == 0 ? 0 : static_cast<int>(total_read * 100 / total_length);
        status.downloaded_bytes = total_read;
        status.total_bytes = total_length;
        status.retry_count = retry_count;
        status.error_code = error_code;
        status.error_message = error_message;
        status_callback(status);
    };

    auto fail = [&](const std::string& code, const std::string& message) {
        final_error_code = code;
        final_error_message = message;
        ESP_LOGE(TAG, "%s: %s", code.c_str(), message.c_str());
    };

    report("downloading");
    bool download_complete = false;
    while (!download_complete && retry_count <= kMaxRetries) {
        auto network = Board::GetInstance().GetNetwork();
        auto http = network->CreateHttp(0);
        http->SetTimeout(kHttpTimeoutMs);
        if (total_read > 0) {
            http->SetHeader("Range", "bytes=" + std::to_string(total_read) + "-");
            ESP_LOGI(TAG, "Resuming OTA download at byte %u (retry %d/%d)",
                     total_read, retry_count, kMaxRetries);
        }

        if (!http->Open("GET", firmware_url)) {
            fail("http_open_failed", "Failed to open HTTP connection");
        } else {
            int status_code = http->GetStatusCode();
            bool status_valid = (total_read == 0 && status_code == 200)
                    || (total_read > 0 && status_code == 206);
            if (!status_valid) {
                fail("range_not_supported", "Unexpected HTTP status " + std::to_string(status_code));
            } else {
                size_t response_length = http->GetBodyLength();
                if (response_length == 0) {
                    fail("missing_content_length", "Firmware response has no Content-Length");
                } else {
                    if (total_read == 0) {
                        total_length = response_length;
                        if (total_length > update_partition->size) {
                            fail("firmware_too_large", "Firmware is larger than OTA partition");
                        }
                    } else {
                        std::string content_range = http->GetResponseHeader("Content-Range");
                        size_t space = content_range.find(' ');
                        size_t dash = content_range.find('-', space == std::string::npos ? 0 : space + 1);
                        size_t slash = content_range.rfind('/');
                        size_t range_start = (space == std::string::npos || dash == std::string::npos) ? SIZE_MAX
                                : static_cast<size_t>(std::strtoull(content_range.c_str() + space + 1, nullptr, 10));
                        size_t range_total = slash == std::string::npos ? 0
                                : static_cast<size_t>(std::strtoull(content_range.c_str() + slash + 1, nullptr, 10));
                        if (range_start != total_read || range_total == 0 || range_total != total_length) {
                            fail("invalid_content_range", "Content-Range does not match firmware size");
                        }
                    }

                    if (final_error_code.empty()) {
                        auto last_calc_time = esp_timer_get_time();
                        while (true) {
                            int ret = http->Read(buffer + buffer_offset, kPageSize - buffer_offset);
                            if (ret < 0) {
                                fail("http_read_timeout", "Firmware download interrupted");
                                break;
                            }

                            recent_read += ret;
                            total_read += ret;
                            buffer_offset += ret;

                            if (!image_header_checked && buffer_offset > 0) {
                                image_header.assign(buffer, buffer_offset);
                                if (image_header.size() >= sizeof(esp_image_header_t)
                                        + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
                                    esp_err_t begin_err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &update_handle);
                                    if (begin_err != ESP_OK) {
                                        fail("ota_begin_failed", esp_err_to_name(begin_err));
                                        break;
                                    }
                                    ota_started = true;
                                    image_header_checked = true;
                                    std::string().swap(image_header);
                                }
                            }

                            bool response_complete = ret == 0;
                            bool firmware_complete = total_length > 0 && total_read == total_length;
                            if (buffer_offset == kPageSize || (firmware_complete && buffer_offset > 0)) {
                                if (!ota_started) {
                                    fail("invalid_image_header", "Firmware image header is incomplete");
                                    break;
                                }
                                esp_err_t write_err = esp_ota_write(update_handle, buffer, buffer_offset);
                                if (write_err != ESP_OK) {
                                    fail("ota_write_failed", esp_err_to_name(write_err));
                                    break;
                                }
                                buffer_offset = 0;
                            }

                            if (esp_timer_get_time() - last_calc_time >= 1000000 || response_complete || firmware_complete) {
                                int progress = total_length == 0 ? 0 : static_cast<int>(total_read * 100 / total_length);
                                ESP_LOGI(TAG, "Progress: %d%% (%u/%u), Speed: %uB/s", progress,
                                         total_read, total_length, recent_read);
                                if (callback) callback(progress, recent_read);
                                if (progress >= last_reported_progress + 5 || progress == 100) {
                                    report("downloading");
                                    last_reported_progress = progress;
                                }
                                last_calc_time = esp_timer_get_time();
                                recent_read = 0;
                            }

                            if (firmware_complete) {
                                download_complete = true;
                                final_error_code.clear();
                                final_error_message.clear();
                                break;
                            }
                            if (response_complete) {
                                fail("incomplete_download", "HTTP response ended before firmware was complete");
                                break;
                            }
                        }
                    }
                }
            }
        }
        http->Close();

        if (download_complete) break;
        if (retry_count >= kMaxRetries || final_error_code == "firmware_too_large"
                || final_error_code == "ota_begin_failed" || final_error_code == "ota_write_failed"
                || final_error_code == "invalid_image_header" || final_error_code == "range_not_supported"
                || final_error_code == "invalid_content_range") {
            break;
        }
        retry_count++;
        report("retrying", final_error_code, final_error_message);
        int backoff_seconds = retry_count * 2;
        ESP_LOGW(TAG, "OTA download retry in %d seconds (%d/%d), resume_offset=%u",
                 backoff_seconds, retry_count, kMaxRetries, total_read);
        vTaskDelay(pdMS_TO_TICKS(backoff_seconds * 1000));
        final_error_code.clear();
        final_error_message.clear();
    }

    heap_caps_free(buffer);
    if (!download_complete || total_read != total_length || !ota_started) {
        if (ota_started) esp_ota_abort(update_handle);
        if (final_error_code.empty()) fail("download_failed", "Firmware download retries exhausted");
        report("failed", final_error_code, final_error_message);
        return false;
    }

    report("downloaded");

    esp_err_t err = esp_ota_end(update_handle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(TAG, "Image validation failed, image is corrupted");
        } else {
            ESP_LOGE(TAG, "Failed to end OTA: %s", esp_err_to_name(err));
        }
        report("failed", "ota_validate_failed", esp_err_to_name(err));
        return false;
    }

    err = esp_ota_set_boot_partition(update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set boot partition: %s", esp_err_to_name(err));
        report("failed", "set_boot_partition_failed", esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "Firmware upgrade successful");
    report("success");
    return true;
}

bool Ota::StartUpgrade(std::function<void(int progress, size_t speed)> callback) {
    return Upgrade(firmware_url_, callback);
}


std::vector<int> Ota::ParseVersion(const std::string& version) {
    std::vector<int> versionNumbers;
    std::stringstream ss(version);
    std::string segment;

    while (std::getline(ss, segment, '.')) {
        versionNumbers.push_back(std::stoi(segment));
    }

    return versionNumbers;
}

bool Ota::IsNewVersionAvailable(const std::string& currentVersion, const std::string& newVersion) {
    std::vector<int> current = ParseVersion(currentVersion);
    std::vector<int> newer = ParseVersion(newVersion);

    for (size_t i = 0; i < std::min(current.size(), newer.size()); ++i) {
        if (newer[i] > current[i]) {
            return true;
        } else if (newer[i] < current[i]) {
            return false;
        }
    }

    return newer.size() > current.size();
}

std::string Ota::GetActivationPayload() {
    if (!has_serial_number_) {
        return "{}";
    }

    std::string hmac_hex;
#ifdef SOC_HMAC_SUPPORTED
    uint8_t hmac_result[32]; // SHA-256 输出为32字节

    // 使用Key0计算HMAC
    esp_err_t ret = esp_hmac_calculate(HMAC_KEY0, (uint8_t*)activation_challenge_.data(), activation_challenge_.size(), hmac_result);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HMAC calculation failed: %s", esp_err_to_name(ret));
        return "{}";
    }

    for (size_t i = 0; i < sizeof(hmac_result); i++) {
        char buffer[3];
        sprintf(buffer, "%02x", hmac_result[i]);
        hmac_hex += buffer;
    }
#endif

    cJSON *payload = cJSON_CreateObject();
    cJSON_AddStringToObject(payload, "algorithm", "hmac-sha256");
    cJSON_AddStringToObject(payload, "serial_number", serial_number_.c_str());
    cJSON_AddStringToObject(payload, "challenge", activation_challenge_.c_str());
    cJSON_AddStringToObject(payload, "hmac", hmac_hex.c_str());
    auto json_str = cJSON_PrintUnformatted(payload);
    std::string json(json_str);
    cJSON_free(json_str);
    cJSON_Delete(payload);

    ESP_LOGI(TAG, "Activation payload: %s", json.c_str());
    return json;
}

esp_err_t Ota::Activate() {
    if (!has_activation_challenge_) {
        ESP_LOGW(TAG, "No activation challenge found");
        return ESP_FAIL;
    }

    std::string url = GetCheckVersionUrl();
    if (url.back() != '/') {
        url += "/activate";
    } else {
        url += "activate";
    }

    auto http = SetupHttp();

    std::string data = GetActivationPayload();
    http->SetContent(std::move(data));

    if (!http->Open("POST", url)) {
        ESP_LOGE(TAG, "Failed to open HTTP connection");
        return ESP_FAIL;
    }

    auto status_code = http->GetStatusCode();
    if (status_code == 202) {
        return ESP_ERR_TIMEOUT;
    }
    if (status_code != 200) {
        ESP_LOGE(TAG, "Failed to activate, code: %d, body: %s", status_code, http->ReadAll().c_str());
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Activation successful");
    return ESP_OK;
}

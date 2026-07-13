#include "protocol.h"

#include <esp_log.h>
#include <esp_app_desc.h>
#include <esp_timer.h>

#define TAG "Protocol"

void Protocol::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    on_incoming_json_ = callback;
}

void Protocol::OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback) {
    on_incoming_audio_ = callback;
}

void Protocol::OnAudioChannelOpened(std::function<void()> callback) {
    on_audio_channel_opened_ = callback;
}

void Protocol::OnAudioChannelClosed(std::function<void()> callback) {
    on_audio_channel_closed_ = callback;
}

void Protocol::OnNetworkError(std::function<void(const std::string& message)> callback) {
    on_network_error_ = callback;
}

void Protocol::OnConnected(std::function<void()> callback) {
    on_connected_ = callback;
}

void Protocol::OnDisconnected(std::function<void()> callback) {
    on_disconnected_ = callback;
}

void Protocol::SetError(const std::string& message) {
    error_occurred_ = true;
    if (on_network_error_ != nullptr) {
        on_network_error_(message);
    }
}

void Protocol::SendAbortSpeaking(AbortReason reason) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
    if (reason == kAbortReasonWakeWordDetected) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendWakeWordDetected(const std::string& wake_word,
                                    const std::string& conversation_id,
                                    const std::string& turn_id) {
    std::string json = "{\"session_id\":\"" + session_id_ + 
                      "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + wake_word + "\"";
    if (!conversation_id.empty()) {
        json += ",\"conversation_id\":\"" + conversation_id + "\"";
    }
    if (!turn_id.empty()) {
        json += ",\"turn_id\":\"" + turn_id + "\"";
    }
    json += "}";
    SendText(json);
}

void Protocol::SendStartListening(ListeningMode mode,
                                  const std::string& conversation_id,
                                  const std::string& turn_id) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";
    if (!conversation_id.empty()) {
        message += ",\"conversation_id\":\"" + conversation_id + "\"";
    }
    if (!turn_id.empty()) {
        message += ",\"turn_id\":\"" + turn_id + "\"";
    }
    if (mode == kListeningModeRealtime) {
        message += ",\"mode\":\"realtime\"";
    } else if (mode == kListeningModeAutoStop) {
        message += ",\"mode\":\"auto\"";
    } else {
        message += ",\"mode\":\"manual\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendStopListening(const std::string& reason,
                                 const std::string& conversation_id,
                                 const std::string& turn_id) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"listen\",\"state\":\"stop\"";
    if (!conversation_id.empty()) {
        message += ",\"conversation_id\":\"" + conversation_id + "\"";
    }
    if (!turn_id.empty()) {
        message += ",\"turn_id\":\"" + turn_id + "\"";
    }
    if (!reason.empty()) {
        message += ",\"reason\":\"" + reason + "\"";
    }
    message += "}";
    SendText(message);
}

void Protocol::SendTtsReady(const std::string& conversation_id, const std::string& turn_id) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"tts\",\"state\":\"ready\"";
    message += ",\"conversation_id\":\"" + conversation_id + "\"";
    message += ",\"turn_id\":\"" + turn_id + "\"}";
    SendText(message);
}

void Protocol::SendMcpMessage(const std::string& payload) {
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    SendText(message);
}

void Protocol::SendDeviceStatus(const std::string& state) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "device_status");
    cJSON_AddStringToObject(root, "state", state.c_str());
    cJSON_AddNumberToObject(root, "uptime_ms", esp_timer_get_time() / 1000);
    cJSON_AddStringToObject(root, "app_version", esp_app_get_description()->version);
    char* json = cJSON_PrintUnformatted(root);
    SendText(json == nullptr ? "{}" : json);
    if (json != nullptr) cJSON_free(json);
    cJSON_Delete(root);
}

void Protocol::SendOtaStatus(const OtaStatus& status, const std::string& version) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "ota");
    cJSON_AddStringToObject(root, "state", status.state.c_str());
    cJSON_AddStringToObject(root, "version", version.c_str());
    cJSON_AddNumberToObject(root, "progress", status.progress);
    cJSON_AddNumberToObject(root, "downloaded_bytes", status.downloaded_bytes);
    cJSON_AddNumberToObject(root, "total_bytes", status.total_bytes);
    cJSON_AddNumberToObject(root, "retry_count", status.retry_count);
    if (!status.error_code.empty()) cJSON_AddStringToObject(root, "error_code", status.error_code.c_str());
    if (!status.error_message.empty()) cJSON_AddStringToObject(root, "error_message", status.error_message.c_str());
    char* json = cJSON_PrintUnformatted(root);
    SendText(json == nullptr ? "{}" : json);
    if (json != nullptr) cJSON_free(json);
    cJSON_Delete(root);
}

bool Protocol::IsTimeout() const {
    const int kTimeoutSeconds = 120;
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_incoming_time_);
    bool timeout = duration.count() > kTimeoutSeconds;
    if (timeout) {
        ESP_LOGE(TAG, "Channel timeout %ld seconds", (long)duration.count());
    }
    return timeout;
}

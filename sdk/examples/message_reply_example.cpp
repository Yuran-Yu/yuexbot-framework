#include "../yuex_plugin_sdk.h"

#include <cstring>
#include <cstdlib>
#include <string>

static const YuexBotApi* g_api = nullptr;
static const char* kPluginId = "message_reply_example";

static std::string json_string_field(const char* jsonText, const char* key) {
    if (!jsonText || !key) return "";
    std::string raw(jsonText);
    std::string needle = std::string("\"") + key + "\":";
    auto pos = raw.find(needle);
    if (pos == std::string::npos) return "";
    pos = raw.find('"', pos + needle.size());
    if (pos == std::string::npos) return "";
    auto end = raw.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return raw.substr(pos + 1, end - pos - 1);
}

static long long json_i64_field(const char* jsonText, const char* key) {
    if (!jsonText || !key) return 0;
    std::string raw(jsonText);
    std::string needle = std::string("\"") + key + "\":";
    auto pos = raw.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '"')) pos++;
    return std::strtoll(raw.c_str() + pos, nullptr, 10);
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_get_info(YuexPluginInfo* info) {
    if (!info) return 0;
    info->abi_version = YUEX_PLUGIN_ABI_VERSION;
    info->id = kPluginId;
    info->name = "Message Reply Example";
    info->version = "1.0.0";
    info->author = "YuexBot";
    info->description = "Multi-account message reply example.";
    return 1;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_init(const YuexBotApi* api) {
    g_api = api;
    if (!g_api || g_api->abi_version < YUEX_PLUGIN_ABI_VERSION) return 0;
    if (g_api->log) g_api->log("info", "message_reply_example loaded");
    return 1;
}

YUEX_PLUGIN_EXPORT void YUEX_PLUGIN_CALL yuex_plugin_shutdown() {
    if (g_api && g_api->log) g_api->log("info", "message_reply_example unloaded");
    g_api = nullptr;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_on_event(int event_type, const char* event_json) {
    if (!g_api || event_type != YUEX_EVENT_MESSAGE || !event_json) return 0;
    std::string raw = json_string_field(event_json, "raw_message");
    if (raw != "/ping") return 0;

    std::string accountId = json_string_field(event_json, "account_id");
    long long groupId = json_i64_field(event_json, "group_id");
    long long userId = json_i64_field(event_json, "user_id");

    int ok = 0;
    if (groupId > 0 && g_api->send_group_msg_as) {
        ok = g_api->send_group_msg_as(accountId.c_str(), groupId, "pong from YuexBot");
    } else if (userId > 0 && g_api->send_private_msg_as) {
        ok = g_api->send_private_msg_as(accountId.c_str(), userId, "pong from YuexBot");
    }
    if (!ok && g_api->get_last_error && g_api->log) g_api->log("error", g_api->get_last_error());
    return ok;
}

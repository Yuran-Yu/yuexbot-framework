#include "../sdk/yuex_plugin_sdk.h"

#include <cstring>
#include <string>

static const YuexBotApi* g_api = nullptr;
static const char* kPluginId = "echo_sample";

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

static int64_t json_int64_field(const char* jsonText, const char* key) {
    if (!jsonText || !key) return 0;
    std::string raw(jsonText);
    std::string needle = std::string("\"") + key + "\":";
    auto pos = raw.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '"')) pos++;
    return _strtoi64(raw.c_str() + pos, nullptr, 10);
}

static void log_info(const char* text) {
    if (g_api && g_api->log) g_api->log("info", text ? text : "");
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_get_info(YuexPluginInfo* info) {
    if (!info) return 0;
    info->abi_version = YUEX_PLUGIN_ABI_VERSION;
    info->id = kPluginId;
    info->name = "Echo Sample";
    info->version = "2.4.1";
    info->author = "YuexBot";
    info->description = "YuexBot ABI v9 sample plugin for multi-account echo, config, permissions, event filters, and normalized event fields.";
    return 1;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_init(const YuexBotApi* api) {
    g_api = api;
    if (!g_api || g_api->abi_version < YUEX_PLUGIN_ABI_VERSION) return 0;

    log_info("Echo sample plugin loaded.");
    if (g_api->set_plugin_config) g_api->set_plugin_config(kPluginId, "prefix", "#echo ");
    if (g_api->get_sdk_info) {
        const char* info = g_api->get_sdk_info();
        if (info && g_api->log) g_api->log("debug", info);
    }
    if (g_api->get_active_account) {
        const char* account = g_api->get_active_account();
        if (account && g_api->log) g_api->log("debug", account);
    }
    if (g_api->has_plugin_permission && g_api->log) {
        g_api->log("debug", g_api->has_plugin_permission(kPluginId, "onebot_api") ? "permission onebot_api=yes" : "permission onebot_api=no");
    }
    if (g_api->set_event_filter) {
        g_api->set_event_filter(kPluginId, (1u << YUEX_EVENT_MESSAGE));
    }
    return 1;
}

YUEX_PLUGIN_EXPORT void YUEX_PLUGIN_CALL yuex_plugin_shutdown() {
    log_info("Echo sample plugin unloaded.");
    g_api = nullptr;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_on_event(int event_type, const char* event_json) {
    if (!g_api || event_type != YUEX_EVENT_MESSAGE || !event_json) return 0;

    const char* prefix = g_api->get_plugin_config
        ? g_api->get_plugin_config(kPluginId, "prefix", "#echo ")
        : "#echo ";
    std::string raw = json_string_field(event_json, "raw_message");
    if (raw.rfind(prefix, 0) != 0) return 0;

    std::string reply = raw.substr(std::strlen(prefix));
    if (reply.empty()) reply = "YuexBot SDK v9";

    int64_t groupId = json_int64_field(event_json, "group_id");
    int64_t userId = json_int64_field(event_json, "user_id");
    std::string accountId = json_string_field(event_json, "account_id");
    std::string groupName = json_string_field(event_json, "group_name");
    std::string senderName = json_string_field(event_json, "sender_name");
    if (g_api->log) {
        std::string detail = "event account=" + accountId + " group=" + groupName + " sender=" + senderName;
        g_api->log("debug", detail.c_str());
    }
    if (userId > 0 && g_api->get_avatar_url && g_api->log) {
        g_api->log("debug", g_api->get_avatar_url(userId, 100));
    }

    if (groupId > 0 && g_api->send_group_msg_as) {
        if (!g_api->send_group_msg_as(accountId.c_str(), groupId, reply.c_str()) && g_api->get_last_error && g_api->log) {
            g_api->log("error", g_api->get_last_error());
        }
        return 1;
    }
    if (userId > 0 && g_api->send_private_msg_as) {
        if (!g_api->send_private_msg_as(accountId.c_str(), userId, reply.c_str()) && g_api->get_last_error && g_api->log) {
            g_api->log("error", g_api->get_last_error());
        }
        return 1;
    }
    return 0;
}

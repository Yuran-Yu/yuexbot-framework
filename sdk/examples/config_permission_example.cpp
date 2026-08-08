#include "../yuex_plugin_sdk.h"

#include <cstring>
#include <string>

static const YuexBotApi* g_api = nullptr;
static const char* kPluginId = "config_permission_example";

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_get_info(YuexPluginInfo* info) {
    if (!info) return 0;
    info->abi_version = YUEX_PLUGIN_ABI_VERSION;
    info->id = kPluginId;
    info->name = "Config Permission Example";
    info->version = "1.0.0";
    info->author = "YuexBot";
    info->description = "Shows plugin permissions and config APIs.";
    return 1;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_init(const YuexBotApi* api) {
    g_api = api;
    if (!g_api || g_api->abi_version < YUEX_PLUGIN_ABI_VERSION) return 0;

    if (g_api->has_plugin_permission && !g_api->has_plugin_permission(kPluginId, "config")) {
        if (g_api->log) g_api->log("error", "missing permission: config");
        return 0;
    }
    if (g_api->set_plugin_config) g_api->set_plugin_config(kPluginId, "hello", "world");
    if (g_api->get_plugin_permissions && g_api->log) {
        g_api->log("debug", g_api->get_plugin_permissions(kPluginId));
    }
    if (g_api->get_data_dir && g_api->log) {
        g_api->log("debug", g_api->get_data_dir(kPluginId));
    }
    if (g_api->log) g_api->log("info", "config_permission_example loaded");
    return 1;
}

YUEX_PLUGIN_EXPORT void YUEX_PLUGIN_CALL yuex_plugin_shutdown() {
    if (g_api && g_api->log) g_api->log("info", "config_permission_example unloaded");
    g_api = nullptr;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_on_event(int event_type, const char* event_json) {
    (void)event_type;
    (void)event_json;
    return 0;
}


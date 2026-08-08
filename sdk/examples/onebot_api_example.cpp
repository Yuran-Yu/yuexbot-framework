#include "../yuex_plugin_sdk.h"

#include <string>

static const YuexBotApi* g_api = nullptr;
static const char* kPluginId = "onebot_api_example";

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_get_info(YuexPluginInfo* info) {
    if (!info) return 0;
    info->abi_version = YUEX_PLUGIN_ABI_VERSION;
    info->id = kPluginId;
    info->name = "OneBot API Example";
    info->version = "1.0.0";
    info->author = "YuexBot";
    info->description = "Shows normalized OneBot API calls.";
    return 1;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_init(const YuexBotApi* api) {
    g_api = api;
    if (!g_api || g_api->abi_version < YUEX_PLUGIN_ABI_VERSION) return 0;
    if (g_api->has_plugin_permission && !g_api->has_plugin_permission(kPluginId, "onebot_api")) {
        if (g_api->log) g_api->log("error", "missing permission: onebot_api");
        return 0;
    }
    if (g_api->get_active_account && g_api->call_onebot_api_as_ex && g_api->log) {
        const char* account = g_api->get_active_account();
        const char* ret = g_api->call_onebot_api_as_ex(account, "get_login_info", "{}");
        g_api->log("debug", ret ? ret : "{}");
    }
    return 1;
}

YUEX_PLUGIN_EXPORT void YUEX_PLUGIN_CALL yuex_plugin_shutdown() {
    g_api = nullptr;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_on_event(int event_type, const char* event_json) {
    (void)event_type;
    (void)event_json;
    return 0;
}


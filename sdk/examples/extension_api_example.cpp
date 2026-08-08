#include "../yuex_plugin_sdk.h"

static const YuexBotApi* g_api = nullptr;

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_get_info(YuexPluginInfo* info) {
    if (!info) return 0;
    info->abi_version = YUEX_PLUGIN_ABI_VERSION;
    info->id = "extension_api_example";
    info->name = "Extension API Example";
    info->version = "1.0.0";
    info->author = "YuexBot";
    info->description = "Shows how to call NapCat/LLBot extension actions through call_onebot_api_as_ex.";
    return 1;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_init(const YuexBotApi* api) {
    g_api = api;
    if (!g_api || g_api->abi_version < YUEX_PLUGIN_ABI_VERSION) return 0;
    if (g_api->log) g_api->log("info", "Extension API example loaded.");
    return 1;
}

YUEX_PLUGIN_EXPORT void YUEX_PLUGIN_CALL yuex_plugin_shutdown() {
    if (g_api && g_api->log) g_api->log("info", "Extension API example unloaded.");
    g_api = nullptr;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_on_event(int event_type, const char* event_json) {
    if (!g_api || event_type != YUEX_EVENT_MESSAGE || !event_json) return 0;
    if (!g_api->call_onebot_api_as_ex || !g_api->log) return 0;

    // This example intentionally does not send messages. It only demonstrates
    // a read-style extension query. Replace "active" with event account_id in
    // real plugins when you parse event_json.
    const char* result = g_api->get_rkey_as
        ? g_api->get_rkey_as("active")
        : g_api->call_onebot_api_as_ex("active", "get_rkey", "{}");
    if (result) g_api->log("debug", result);
    else if (g_api->get_last_error) g_api->log("error", g_api->get_last_error());
    return 0;
}

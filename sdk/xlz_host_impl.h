#pragma once
// xlz_host_impl.h - YuexBot host-side XiaoLiZi compatibility layer
// Provides Chinese-named API functions and apprun plugin loader

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <windows.h>
#include "../third_party/json.hpp"
#include "xlz_compat_sdk.h"

using json = nlohmann::json;

// Forward declarations (defined in main.cpp)
// Forward declarations for functions defined later in main.cpp
extern std::string json_value_string(const json& j, const std::string& key, const std::string& fallback = "");
extern int64_t json_value_i64(const json& j, const std::string& key, int64_t fallback = 0);
extern std::string normalize_external_text(const std::string& s);
extern json call_onebot_api_bridge(const std::string& action, const json& params = json::object());
extern void add_log(const std::string&, const std::string&, const std::string&,
                    int64_t sender_id = 0, int64_t group_id = 0,
                    const std::string& account_id = "", const std::string& account_name = "",
                    const json& detail = json::object());
extern HWND g_mainHwnd;

// ============ XiaoLiZi API function implementations ============
// These functions follow the XiaoLiZi calling convention:
//   - First parameter is pluginKey (const char*)
//   - Return type matches the API type
//   - All string I/O uses UTF-8 (YuexBot native encoding)

static std::mutex g_xlzApiMutex;
static std::string g_xlzApiResult; // Persistent storage for returned strings

static const char* xlz_store(const std::string& s) {
    std::lock_guard<std::mutex> lock(g_xlzApiMutex);
    g_xlzApiResult = s;
    return g_xlzApiResult.c_str();
}

static const char* xlz_store_json(const json& j) {
    return xlz_store(j.dump());
}

static int xlz_ok(const json& r) {
    if (r.contains("retcode")) {
        int rc = r["retcode"].is_number() ? r["retcode"].get<int>() : -1;
        return (rc == 0) ? 1 : 0;
    }
    if (r.contains("status")) {
        return r["status"].get<std::string>() == "ok" ? 1 : 0;
    }
    return 0;
}

// ---- OutputLog ----
static const char* XLZ_CALL xlz_fn_OutputLog(const char* pluginKey, const char* msg, uint32_t fg, uint32_t bg) {
    (void)pluginKey; (void)fg; (void)bg;
    add_log("插件", "", msg ? msg : "");
    return "";
}

// ---- SendFriendMessage ----
static const char* XLZ_CALL xlz_fn_SendFriendMessage(const char* pluginKey, uint64_t thisQQ, uint64_t friendQQ, const char* msg, int64_t* outRandom, uint32_t* outReq) {
    (void)pluginKey;
    json p;
    p["user_id"] = friendQQ;
    p["message"] = msg ? msg : "";
    auto r = call_onebot_api_bridge("send_private_msg", p);
    if (outRandom) *outRandom = json_value_i64(r, "data.message_id", 0);
    if (outReq) *outReq = (uint32_t)json_value_i64(r, "data.message_id", 0);
    return xlz_store_json(r);
}

// ---- SendGroupMessage ----
static const char* XLZ_CALL xlz_fn_SendGroupMessage(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, const char* msg, uint32_t anonymous) {
    (void)pluginKey; (void)thisQQ; (void)anonymous;
    json p;
    p["group_id"] = groupQQ;
    p["message"] = msg ? msg : "";
    return xlz_store_json(call_onebot_api_bridge("send_group_msg", p));
}

// ---- GetFrameworkQQ ----
static const char* XLZ_CALL xlz_fn_GetFrameworkQQ(const char* pluginKey) {
    (void)pluginKey;
    return xlz_store_json(call_onebot_api_bridge("get_login_info"));
}

// ---- GetGroupList ----
static uint32_t XLZ_CALL xlz_fn_GetGroupList(const char* pluginKey, uint64_t thisQQ, void* outBlocks) {
    (void)pluginKey; (void)thisQQ; (void)outBlocks;
    // Return count; actual list requires struct packing - return 0 for now
    // Plugins should use CallOneBotInterface for full group list
    return 0;
}

// ---- GetGroupMemberList ----
static uint32_t XLZ_CALL xlz_fn_GetGroupMemberList(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, void* outBlocks) {
    (void)pluginKey; (void)thisQQ; (void)groupQQ; (void)outBlocks;
    return 0;
}

// ---- SendGroupTemporaryMessage ----
static const char* XLZ_CALL xlz_fn_SendGroupTemporaryMessage(const char* pluginKey, uint64_t thisQQ, uint64_t groupId, uint64_t otherQQ, const char* content, int64_t* outRandom, int32_t* outReq) {
    (void)pluginKey;
    json p;
    p["user_id"] = otherQQ;
    p["group_id"] = groupId;
    p["message"] = content ? content : "";
    auto r = call_onebot_api_bridge("send_private_msg", p);
    if (outRandom) *outRandom = json_value_i64(r, "data.message_id", 0);
    if (outReq) *outReq = (int32_t)json_value_i64(r, "data.message_id", 0);
    return xlz_store_json(r);
}

// ---- SendGroupJsonMessage ----
static const char* XLZ_CALL xlz_fn_SendGroupJsonMessage(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, const char* jsonMsg, uint32_t anonymous) {
    (void)pluginKey; (void)thisQQ; (void)anonymous;
    json p;
    p["group_id"] = groupQQ;
    // Json message as a CQ code segment
    p["message"] = jsonMsg ? jsonMsg : "";
    return xlz_store_json(call_onebot_api_bridge("send_group_msg", p));
}

// ---- MuteGroupMember ----
static uint32_t XLZ_CALL xlz_fn_MuteGroupMember(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, uint32_t duration) {
    (void)pluginKey; (void)thisQQ;
    json p;
    p["group_id"] = groupQQ;
    p["user_id"] = memberQQ;
    p["duration"] = duration;
    return xlz_ok(call_onebot_api_bridge("set_group_ban", p));
}

// ---- RemoveGroupMember ----
static uint32_t XLZ_CALL xlz_fn_RemoveGroupMember(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, uint32_t reject) {
    (void)pluginKey; (void)thisQQ;
    json p;
    p["group_id"] = groupQQ;
    p["user_id"] = memberQQ;
    p["reject_add_request"] = reject != 0;
    return xlz_ok(call_onebot_api_bridge("set_group_kick", p));
}

// ---- RecallGroupMessage ----
static uint32_t XLZ_CALL xlz_fn_RecallGroupMessage(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, int64_t msgSeq) {
    (void)pluginKey; (void)thisQQ; (void)groupQQ;
    json p;
    p["message_id"] = (int32_t)msgSeq;
    return xlz_ok(call_onebot_api_bridge("delete_msg", p));
}

// ---- MuteAll ----
static uint32_t XLZ_CALL xlz_fn_MuteAll(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint32_t enable) {
    (void)pluginKey; (void)thisQQ;
    json p;
    p["group_id"] = groupQQ;
    p["enable"] = enable != 0;
    return xlz_ok(call_onebot_api_bridge("set_group_whole_ban", p));
}

// ---- QQLike ----
static uint32_t XLZ_CALL xlz_fn_QQLike(const char* pluginKey, uint64_t thisQQ, uint64_t targetQQ) {
    (void)pluginKey; (void)thisQQ;
    json p;
    p["user_id"] = targetQQ;
    p["times"] = 1;
    return xlz_ok(call_onebot_api_bridge("send_like", p));
}

// ---- GetGroupCard ----
static const char* XLZ_CALL xlz_fn_GetGroupCard(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ) {
    (void)pluginKey; (void)thisQQ;
    json p;
    p["group_id"] = groupQQ;
    p["user_id"] = memberQQ;
    auto r = call_onebot_api_bridge("get_group_member_info", p);
    if (r.contains("data") && r["data"].is_object() && r["data"].contains("card")) {
        return xlz_store(r["data"]["card"].get<std::string>());
    }
    return xlz_store("");
}

// ---- SetGroupCard ----
static uint32_t XLZ_CALL xlz_fn_SetGroupCard(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t memberQQ, const char* card) {
    (void)pluginKey; (void)thisQQ;
    json p;
    p["group_id"] = groupQQ;
    p["user_id"] = memberQQ;
    p["card"] = card ? card : "";
    return xlz_ok(call_onebot_api_bridge("set_group_card", p));
}

// ---- GetNicknameForce ----
static const char* XLZ_CALL xlz_fn_GetNicknameForce(const char* pluginKey, uint64_t thisQQ, uint64_t targetQQ) {
    (void)pluginKey; (void)thisQQ;
    json p;
    p["user_id"] = targetQQ;
    auto r = call_onebot_api_bridge("get_stranger_info", p);
    if (r.contains("data") && r["data"].is_object() && r["data"].contains("nickname")) {
        return xlz_store(r["data"]["nickname"].get<std::string>());
    }
    return xlz_store("");
}

// ---- GetGroupMemberInfo ----
static const char* XLZ_CALL xlz_fn_GetGroupMemberInfo(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t otherQQ, void* outData) {
    (void)pluginKey; (void)thisQQ; (void)outData;
    json p;
    p["group_id"] = groupQQ;
    p["user_id"] = otherQQ;
    p["no_cache"] = false;
    return xlz_store_json(call_onebot_api_bridge("get_group_member_info", p));
}

// ---- GetPluginDataDirectory ----
static const char* XLZ_CALL xlz_fn_GetPluginDataDirectory(const char* pluginKey) {
    std::string dir = std::string("data\\plugins\\") + (pluginKey ? pluginKey : "unknown") + "\\";
    CreateDirectoryA("data", NULL);
    CreateDirectoryA("data\\plugins", NULL);
    CreateDirectoryA(dir.c_str(), NULL);
    return xlz_store(dir);
}

// ---- GetPluginSelfVersion ----
static const char* XLZ_CALL xlz_fn_GetPluginSelfVersion(const char* pluginKey) {
    (void)pluginKey;
    return xlz_store("1.0.0");
}

// ---- GetFrameworkMainWindowHandle ----
static uint32_t XLZ_CALL xlz_fn_GetFrameworkMainWindowHandle(const char* pluginKey) {
    (void)pluginKey;
    // g_mainHwnd defined in main.cpp
    return (uint32_t)(uintptr_t)g_mainHwnd;
}

// ---- GetQQAvatar ----
static const char* XLZ_CALL xlz_fn_GetQQAvatar(const char* pluginKey, uint64_t otherQQ, uint32_t hd) {
    (void)pluginKey; (void)hd;
    std::string url = "https://q.qlogo.cn/g?b=qq&nk=" + std::to_string(otherQQ) + "&s=640";
    return xlz_store(url);
}

// ---- GetPluginFileName ----
static const char* XLZ_CALL xlz_fn_GetPluginFileName(const char* pluginKey) {
    (void)pluginKey;
    return xlz_store(pluginKey ? pluginKey : "");
}

// ---- GetFrameworkVersion ----
static const char* XLZ_CALL xlz_fn_GetFrameworkVersion(const char* pluginKey) {
    (void)pluginKey;
    return xlz_store("YuexBot 1.0.0");
}

// ---- GetCurrentOneBotClientType ----
static const char* XLZ_CALL xlz_fn_GetCurrentOneBotClientType(const char* pluginKey, uint64_t thisQQ) {
    (void)pluginKey; (void)thisQQ;
    return xlz_store("YuexBot");
}

// ---- CallOneBotInterface ----
static const char* XLZ_CALL xlz_fn_CallOneBotInterface(const char* pluginKey, uint64_t thisQQ, const char* sendData, uint32_t noWait) {
    (void)pluginKey; (void)thisQQ; (void)noWait;
    if (!sendData || !*sendData) return xlz_store("{}");
    try {
        json req = json::parse(sendData);
        std::string action = req.value("action", "");
        json params = req.value("params", json::object());
        return xlz_store_json(call_onebot_api_bridge(action, params));
    } catch (...) {
        return xlz_store("{\"error\":\"invalid json\"}");
    }
}

// ---- HandleFriendVerificationEvent ----
static uint32_t XLZ_CALL xlz_fn_HandleFriendVerificationEvent(const char* pluginKey, uint64_t thisQQ, uint64_t sourceQQ, uint32_t eventType, const char* seq, uint32_t accept) {
    (void)pluginKey; (void)thisQQ; (void)eventType;
    json p;
    p["flag"] = seq ? seq : "";
    p["approve"] = accept != 0;
    return xlz_ok(call_onebot_api_bridge("set_friend_add_request", p));
}

// ---- HandleGroupVerificationEvent ----
static uint32_t XLZ_CALL xlz_fn_HandleGroupVerificationEvent(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint64_t sourceQQ, uint32_t eventType, const char* seq, uint32_t accept) {
    (void)pluginKey; (void)thisQQ; (void)groupQQ; (void)sourceQQ;
    json p;
    p["flag"] = seq ? seq : "";
    p["sub_type"] = (eventType == 1) ? "add" : "invite";
    p["approve"] = accept != 0;
    return xlz_ok(call_onebot_api_bridge("set_group_add_request", p));
}

// ---- GetAdministratorList ----
static const char* XLZ_CALL xlz_fn_GetAdministratorList(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ) {
    (void)pluginKey; (void)thisQQ;
    json p;
    p["group_id"] = groupQQ;
    return xlz_store_json(call_onebot_api_bridge("get_group_member_list", p));
}

// ---- ReloadItSelf ----
static void XLZ_CALL xlz_fn_ReloadItSelf(const char* pluginKey, const char* dllPath) {
    (void)pluginKey; (void)dllPath;
    // Not implemented in host - plugin reload handled by framework
}

// ---- UploadFriendImage ----
static const char* XLZ_CALL xlz_fn_UploadFriendImage(const char* pluginKey, uint64_t thisQQ, uint64_t friendQQ, uint32_t flash, const void* pic, uint32_t size, int32_t w, int32_t h, uint32_t cartoon, const char* preview) {
    (void)pluginKey; (void)thisQQ; (void)friendQQ; (void)flash; (void)pic; (void)size; (void)w; (void)h; (void)cartoon; (void)preview;
    return xlz_store("[CQ:image,file=base64://...]");
}

// ---- UploadGroupImage ----
static const char* XLZ_CALL xlz_fn_UploadGroupImage(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, uint32_t flash, const void* pic, uint32_t size, int32_t w, int32_t h, uint32_t cartoon, const char* preview) {
    (void)pluginKey; (void)thisQQ; (void)groupQQ; (void)flash; (void)pic; (void)size; (void)w; (void)h; (void)cartoon; (void)preview;
    return xlz_store("[CQ:image,file=base64://...]");
}

// ============ Build apidata JSON ============
static std::string build_xlz_apidata() {
    json j;
    j[xlz::kApi_OutputLog]             = (uint64_t)(uintptr_t)&xlz_fn_OutputLog;
    j[xlz::kApi_SendFriendMessage]     = (uint64_t)(uintptr_t)&xlz_fn_SendFriendMessage;
    j[xlz::kApi_SendGroupMessage]      = (uint64_t)(uintptr_t)&xlz_fn_SendGroupMessage;
    j[xlz::kApi_GetFrameworkQQ]        = (uint64_t)(uintptr_t)&xlz_fn_GetFrameworkQQ;
    j[xlz::kApi_GetGroupList]          = (uint64_t)(uintptr_t)&xlz_fn_GetGroupList;
    j[xlz::kApi_GetGroupMemberList]    = (uint64_t)(uintptr_t)&xlz_fn_GetGroupMemberList;
    j[xlz::kApi_SendGroupTemporaryMsg] = (uint64_t)(uintptr_t)&xlz_fn_SendGroupTemporaryMessage;
    j[xlz::kApi_SendGroupJsonMessage]  = (uint64_t)(uintptr_t)&xlz_fn_SendGroupJsonMessage;
    j[xlz::kApi_MuteGroupMember]       = (uint64_t)(uintptr_t)&xlz_fn_MuteGroupMember;
    j[xlz::kApi_RemoveGroupMember]     = (uint64_t)(uintptr_t)&xlz_fn_RemoveGroupMember;
    j[xlz::kApi_RecallGroupMessage]    = (uint64_t)(uintptr_t)&xlz_fn_RecallGroupMessage;
    j[xlz::kApi_MuteAll]              = (uint64_t)(uintptr_t)&xlz_fn_MuteAll;
    j[xlz::kApi_QQLike]               = (uint64_t)(uintptr_t)&xlz_fn_QQLike;
    j[xlz::kApi_GetGroupCard]         = (uint64_t)(uintptr_t)&xlz_fn_GetGroupCard;
    j[xlz::kApi_SetGroupCard]         = (uint64_t)(uintptr_t)&xlz_fn_SetGroupCard;
    j[xlz::kApi_GetNicknameForce]     = (uint64_t)(uintptr_t)&xlz_fn_GetNicknameForce;
    j[xlz::kApi_GetGroupMemberInfo]   = (uint64_t)(uintptr_t)&xlz_fn_GetGroupMemberInfo;
    j[xlz::kApi_GetPluginDataDir]     = (uint64_t)(uintptr_t)&xlz_fn_GetPluginDataDirectory;
    j[xlz::kApi_GetPluginSelfVersion] = (uint64_t)(uintptr_t)&xlz_fn_GetPluginSelfVersion;
    j[xlz::kApi_GetFrameworkMainWnd]  = (uint64_t)(uintptr_t)&xlz_fn_GetFrameworkMainWindowHandle;
    j[xlz::kApi_GetQQAvatar]          = (uint64_t)(uintptr_t)&xlz_fn_GetQQAvatar;
    j[xlz::kApi_GetPluginFileName]    = (uint64_t)(uintptr_t)&xlz_fn_GetPluginFileName;
    j[xlz::kApi_GetFrameworkVersion]  = (uint64_t)(uintptr_t)&xlz_fn_GetFrameworkVersion;
    j[xlz::kApi_GetCurrentOBType]     = (uint64_t)(uintptr_t)&xlz_fn_GetCurrentOneBotClientType;
    j[xlz::kApi_CallOneBotInterface]  = (uint64_t)(uintptr_t)&xlz_fn_CallOneBotInterface;
    j[xlz::kApi_HandleFriendVerify]   = (uint64_t)(uintptr_t)&xlz_fn_HandleFriendVerificationEvent;
    j[xlz::kApi_HandleGroupVerify]    = (uint64_t)(uintptr_t)&xlz_fn_HandleGroupVerificationEvent;
    j[xlz::kApi_GetAdminList]         = (uint64_t)(uintptr_t)&xlz_fn_GetAdministratorList;
    j[xlz::kApi_ReloadItSelf]         = (uint64_t)(uintptr_t)&xlz_fn_ReloadItSelf;
    j[xlz::kApi_UploadFriendImage]    = (uint64_t)(uintptr_t)&xlz_fn_UploadFriendImage;
    j[xlz::kApi_UploadGroupImage]     = (uint64_t)(uintptr_t)&xlz_fn_UploadGroupImage;
    return j.dump();
}

// ============ XiaoLiZi plugin runtime ============
struct XlzPluginRuntime {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string path;
    bool enabled = false;
    HMODULE module = nullptr;
    // Callbacks (from plugin's apprun return JSON)
    int  (*fnGroupMsg)(void*) = nullptr;
    int  (*fnPrivateMsg)(void*) = nullptr;
    int  (*fnEventMsg)(void*) = nullptr;
    int  (*fnPluginEnable)() = nullptr;
    void (*fnPluginDisable)() = nullptr;
    void (*fnPluginUninstall)() = nullptr;
    int  (*fnAppSetting)() = nullptr;
};

static std::vector<XlzPluginRuntime> g_xlzPlugins;
static std::mutex g_xlzPluginMutex;
static std::string g_xlzApidata;
static bool g_xlzApidataReady = false;

static void ensure_xlz_apidata() {
    if (!g_xlzApidataReady) {
        g_xlzApidata = build_xlz_apidata();
        g_xlzApidataReady = true;
    }
}

// Parse apprun return JSON (AppInfo format)
static void parse_xlz_appinfo(const std::string& jsonStr, XlzPluginRuntime& p) {
    if (jsonStr.empty()) return;
    try {
        json j = json::parse(jsonStr);
        if (j.contains("name")) p.name = normalize_external_text(j["name"].get<std::string>());
        if (j.contains("version")) p.version = normalize_external_text(j["version"].get<std::string>());
        if (j.contains("author")) p.author = normalize_external_text(j["author"].get<std::string>());
        if (j.contains("description")) p.description = normalize_external_text(j["description"].get<std::string>());

        // Parse callback function addresses
        if (j.contains("groupmsaddres")) {
            uint64_t addr = j["groupmsaddres"].is_number() ? j["groupmsaddres"].get<uint64_t>() : 0;
            if (addr) p.fnGroupMsg = (int(*)(void*))(uintptr_t)addr;
        }
        if (j.contains("friendmsaddres")) {
            uint64_t addr = j["friendmsaddres"].is_number() ? j["friendmsaddres"].get<uint64_t>() : 0;
            if (addr) p.fnPrivateMsg = (int(*)(void*))(uintptr_t)addr;
        }
        if (j.contains("eventmsaddres")) {
            uint64_t addr = j["eventmsaddres"].is_number() ? j["eventmsaddres"].get<uint64_t>() : 0;
            if (addr) p.fnEventMsg = (int(*)(void*))(uintptr_t)addr;
        }
        if (j.contains("useproaddres")) {
            uint64_t addr = j["useproaddres"].is_number() ? j["useproaddres"].get<uint64_t>() : 0;
            if (addr) p.fnPluginEnable = (int(*)())(uintptr_t)addr;
        }
        if (j.contains("banproaddres")) {
            uint64_t addr = j["banproaddres"].is_number() ? j["banproaddres"].get<uint64_t>() : 0;
            if (addr) p.fnPluginDisable = (void(*)())(uintptr_t)addr;
        }
        if (j.contains("unitproaddres")) {
            uint64_t addr = j["unitproaddres"].is_number() ? j["unitproaddres"].get<uint64_t>() : 0;
            if (addr) p.fnPluginUninstall = (void(*)())(uintptr_t)addr;
        }
        if (j.contains("setproaddres")) {
            uint64_t addr = j["setproaddres"].is_number() ? j["setproaddres"].get<uint64_t>() : 0;
            if (addr) p.fnAppSetting = (int(*)())(uintptr_t)addr;
        }
    } catch (...) {}
}

// Try to load a DLL as XiaoLiZi plugin (apprun entry point)
static bool try_load_xlz_plugin(const std::string& path, bool enable) {
    HMODULE mod = LoadLibraryA(path.c_str());
    if (!mod) return false;

    // Check for apprun export
    typedef const char* (*AppRunFn)(const char*, const char*);
    auto apprun = (AppRunFn)GetProcAddress(mod, "apprun");
    if (!apprun) {
        FreeLibrary(mod);
        return false;
    }

    ensure_xlz_apidata();

    // Build pluginKey from filename
    std::string pluginKey = path;
    auto bs = pluginKey.rfind('\\');
    if (bs != std::string::npos) pluginKey = pluginKey.substr(bs + 1);
    auto dot = pluginKey.rfind('.');
    if (dot != std::string::npos) pluginKey = pluginKey.substr(0, dot);

    XlzPluginRuntime p;
    p.path = path;
    p.id = pluginKey;
    p.module = mod;

    // Call apprun(pluginkey, apidata)
    const char* result = nullptr;
    try {
        result = apprun(pluginKey.c_str(), g_xlzApidata.c_str());
    } catch (...) {
        FreeLibrary(mod);
        return false;
    }

    // Parse the returned AppInfo JSON
    parse_xlz_appinfo(result ? result : "", p);

    if (p.name.empty()) p.name = pluginKey;

    // Enable plugin
    if (enable && p.fnPluginEnable) {
        try { p.fnPluginEnable(); } catch (...) {}
    }
    p.enabled = enable;

    {
        std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
        g_xlzPlugins.push_back(p);
    }
    // Trace: startup_trace defined in main.cpp
    return true;
}

// Forward a group message to all XiaoLiZi plugins
static void xlz_dispatch_group_msg(const json& evt) {
    xlz::GroupMessageEvent ge = {};
    ge.SenderQQ = json_value_i64(evt, "sender.user_id");
    ge.ThisQQ = json_value_i64(evt, "self_id");
    ge.MessageReq = 0;
    ge.MessageReceiveTime = (int32_t)json_value_i64(evt, "time");
    ge.MessageGroupQQ = json_value_i64(evt, "group_id");
    ge.SourceGroupName = "";
    ge.SenderNickname = "";
    if (evt.contains("sender") && evt["sender"].is_object()) {
        static std::string nick;
        nick = json_value_string(evt["sender"], "nickname");
        ge.SenderNickname = nick.c_str();
    }
    ge.MessageSendTime = (int32_t)json_value_i64(evt, "time");
    ge.MessageRandom = 0;
    ge.MessageClip = 0;
    ge.MessageClipCount = 0;
    ge.MessageClipID = 0;
    ge.MessageType = 0;
    ge.SenderTitle = "";
    // Extract message content as text
    static std::string content;
    content = json_value_string(evt, "raw_message");
    if (content.empty()) content = json_value_string(evt, "message");
    ge.MessageContent = content.c_str();
    ge.ReplyMessageContent = "";
    ge.BubbleID = 0;
    ge.GroupChatLevel = 0;
    ge.PendantID = 0;
    ge.AnonymousNickname = "";
    ge.AnonymousFlag = nullptr;
    ge.ReservedParameters = "";
    ge.AnonymousId = 0;
    ge.FontId = 0;

    std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
    for (auto& p : g_xlzPlugins) {
        if (!p.enabled || !p.fnGroupMsg) continue;
        try { p.fnGroupMsg(&ge); } catch (...) {}
    }
}

// Forward a private message to all XiaoLiZi plugins
static void xlz_dispatch_private_msg(const json& evt) {
    xlz::PrivateMessageEvent pe = {};
    pe.SenderQQ = json_value_i64(evt, "sender.user_id");
    pe.ThisQQ = json_value_i64(evt, "self_id");
    pe.MessageReq = 0;
    pe.MessageSeq = json_value_i64(evt, "message_id");
    pe.MessageReceiveTime = (uint32_t)json_value_i64(evt, "time");
    pe.MessageGroupQQ = 0;
    pe.MessageSendTime = (uint32_t)json_value_i64(evt, "time");
    pe.MessageRandom = 0;
    pe.MessageClip = 0;
    pe.MessageClipCount = 0;
    pe.MessageClipID = 0;
    static std::string content;
    content = json_value_string(evt, "raw_message");
    if (content.empty()) content = json_value_string(evt, "message");
    pe.MessageContent = content.c_str();
    pe.BubbleID = 0;
    pe.MessageType = 0;
    pe.MessageSubType = 0;
    pe.MessageSubTemporaryType = 0;
    pe.RedEnvelopeType = 0;
    pe.SessionToken = nullptr;
    pe.SourceEventQQ = 0;
    pe.SourceEventQQName = "";
    pe.FileID = "";
    pe.FileMD5 = "";
    pe.FileName = "";
    pe.MsgGroupId = 0;

    std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
    for (auto& p : g_xlzPlugins) {
        if (!p.enabled || !p.fnPrivateMsg) continue;
        try { p.fnPrivateMsg(&pe); } catch (...) {}
    }
}

// Forward notice/request events to all XiaoLiZi plugins
static void xlz_dispatch_event(const json& evt) {
    xlz::EventTypeBase eb = {};
    eb.ThisQQ = json_value_i64(evt, "self_id");
    eb.SourceGroupQQ = json_value_i64(evt, "group_id");
    eb.OperateQQ = json_value_i64(evt, "operator_id");
    if (eb.OperateQQ == 0) eb.OperateQQ = json_value_i64(evt, "user_id");
    eb.TriggerQQ = json_value_i64(evt, "user_id");
    eb.MessageSeq = json_value_i64(evt, "message_id");
    eb.MessageTime = (uint32_t)json_value_i64(evt, "time");
    static std::string srcGroupName, opName, trigName, content;
    srcGroupName = ""; opName = ""; trigName = "";
    content = json_value_string(evt, "raw_message");
    if (content.empty()) content = json_value_string(evt, "message");
    if (content.empty()) content = json_value_string(evt, "notice_type");
    eb.SourceGroupName = srcGroupName.c_str();
    eb.OperateQQName = opName.c_str();
    eb.TriggerQQName = trigName.c_str();
    eb.MessageContent = content.c_str();

    // Map event type
    std::string noticeType = json_value_string(evt, "notice_type");
    std::string requestType = json_value_string(evt, "request_type");
    eb.EventType = 0;
    eb.EventSubType = 0;
    if (noticeType == "group_increase") eb.EventType = 1;
    else if (noticeType == "group_decrease") eb.EventType = 2;
    else if (noticeType == "group_admin") eb.EventType = 3;
    else if (noticeType == "group_ban") eb.EventType = 4;
    else if (noticeType == "friend_add") eb.EventType = 5;
    else if (requestType == "friend") eb.EventType = 10;
    else if (requestType == "group") eb.EventType = 11;

    std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
    for (auto& p : g_xlzPlugins) {
        if (!p.enabled || !p.fnEventMsg) continue;
        try { p.fnEventMsg(&eb); } catch (...) {}
    }
}

// Dispatch event to XiaoLiZi plugins (called from process_event)
static void xlz_dispatch_plugin_event(const json& evt) {
    std::string postType = json_value_string(evt, "post_type");
    if (postType == "message") {
        std::string msgType = json_value_string(evt, "message_type");
        if (msgType == "group") xlz_dispatch_group_msg(evt);
        else if (msgType == "private") xlz_dispatch_private_msg(evt);
    } else {
        xlz_dispatch_event(evt);
    }
}

// Unload all XiaoLiZi plugins
static void xlz_unload_all() {
    std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
    for (auto& p : g_xlzPlugins) {
        if (p.enabled && p.fnPluginDisable) {
            try { p.fnPluginDisable(); } catch (...) {}
        }
        if (p.module) {
            FreeLibrary(p.module);
            p.module = nullptr;
        }
    }
    g_xlzPlugins.clear();
}

// echo_xlz_plugin.cpp - Example XiaoLiZi-compatible plugin for YuexBot
// Compile: g++ -shared -static -o echo_xlz_plugin.dll echo_xlz_plugin.cpp -I. -Wno-everything

#define XLZSDK_EXPORTS
#include "sdk/XiaoLiZiVM_CppSDK.h"

#include <cstring>
#include <string>

static xlz::AppRunContext g_ctx;
static const char* g_appName = "Echo Plugin (XLZ)";
static const char* g_appVersion = "1.0.0";
static const char* g_appAuthor = "YuexBot";
static const char* g_appDesc = "Echo plugin using XiaoLiZi SDK compatibility";

// AppInfo JSON returned by apprun
static std::string g_appInfoJson;

static int OnGroupMessage(const xlz::GroupMessageEvent* ev) {
    if (!ev || !ev->MessageContent) return 0;
    std::string content(ev->MessageContent);
    
    // Echo: if message starts with "echo ", reply with the rest
    if (content.size() > 5 && content.substr(0, 5) == "echo ") {
        std::string reply = content.substr(5);
        xlz::SendGroupMessage(ev->ThisQQ, ev->MessageGroupQQ, reply.c_str());
    }
    
    // Ping pong
    if (content == "ping") {
        xlz::SendGroupMessage(ev->ThisQQ, ev->MessageGroupQQ, "pong");
    }
    
    return 0;
}

static int OnPrivateMessage(const xlz::PrivateMessageEvent* ev) {
    if (!ev || !ev->MessageContent) return 0;
    std::string content(ev->MessageContent);
    
    if (content == "ping") {
        xlz::SendPrivateMessage(ev->ThisQQ, ev->SenderQQ, "pong (from private)");
    }
    
    return 0;
}

static int OnEvent(const xlz::EventTypeBase* ev) {
    (void)ev;
    return 0;
}

static int OnPluginEnable() {
    xlz::OutputLog("Echo Plugin enabled!");
    return 1;
}

static void OnPluginDisable() {
    // Cleanup
}

static void OnPluginUninstall() {
    // Cleanup
}

static int OnAppSetting() {
    return 0;
}

// XiaoLiZi entry point
XLZ_API const char* XLZ_CALL apprun(const char* a, const char* b) {
    // Store the apidata and pluginKey
    const char* apiData = nullptr;
    const char* pluginKey = nullptr;
    
    // Detect which is which (apidata contains JSON with function addresses)
    auto looksLikeApiData = [](const char* s) -> bool {
        if (!s) return false;
        if (std::strchr(s, '{') == nullptr || std::strchr(s, ':') == nullptr) return false;
        return true;
    };
    
    if (looksLikeApiData(a)) { apiData = a; pluginKey = b; }
    else if (looksLikeApiData(b)) { apiData = b; pluginKey = a; }
    else { apiData = a; pluginKey = b; }
    
    // Parse apidata to get function addresses and set up the context
    // The apidata JSON maps Chinese API names to function pointer addresses
    // We need to parse it and populate the AppRunContext
    
    // Simple JSON parser for address extraction
    auto extractAddr = [](const char* json, const char* key) -> uintptr_t {
        if (!json || !key) return 0;
        std::string search = std::string("\"") + key + "\"";
        const char* pos = std::strstr(json, search.c_str());
        if (!pos) return 0;
        pos += search.size();
        while (*pos && (*pos == ' ' || *pos == ':')) pos++;
        uintptr_t addr = 0;
        while (*pos >= '0' && *pos <= '9') {
            addr = addr * 10 + (*pos - '0');
            pos++;
        }
        return addr;
    };
    
    auto& ctx = xlz::GetContext();
    ctx.pluginKey = pluginKey ? pluginKey : "echo_xlz";
    
    if (apiData) {
        ctx.OutputLog                    = (xlz::Fn_OutputLog)extractAddr(apiData, "\xE8\xBE\x93\xE5\x87\xBA\xE6\x97\xA5\xE5\xBF\x97");
        ctx.SendFriendMessage            = (xlz::Fn_SendFriendMessage)extractAddr(apiData, "\xE5\x8F\x91\xE9\x80\x81\xE5\xA5\xBD\xE5\x8F\x8B\xE6\xB6\x88\xE6\x81\xAF");
        ctx.SendGroupMessage             = (xlz::Fn_SendGroupMessage)extractAddr(apiData, "\xE5\x8F\x91\xE9\x80\x81\xE7\xBE\xA4\xE6\xB6\x88\xE6\x81\xAF");
        ctx.GetFrameworkQQ               = (xlz::Fn_GetFrameworkQQ)extractAddr(apiData, "\xE5\x8F\x96\xE6\xA1\x86\xE6\x9E\xB6\x51\x51");
        ctx.GetGroupList                 = (xlz::Fn_GetGroupList)extractAddr(apiData, "\xE5\x8F\x96\xE7\xBE\xA4\xE5\x88\x97\xE8\xA1\xA8");
        ctx.GetGroupMemberList           = (xlz::Fn_GetGroupMemberList)extractAddr(apiData, "\xE5\x8F\x96\xE7\xBE\xA4\xE6\x88\x90\xE5\x91\x98\xE5\x88\x97\xE8\xA1\xA8");
        ctx.SendGroupTemporaryMessage    = (xlz::Fn_SendGroupTemporaryMessage)extractAddr(apiData, "\xE5\x8F\x91\xE9\x80\x81\xE7\xBE\xA4\xE4\xB8\xB4\xE6\x97\xB6\xE6\xB6\x88\xE6\x81\xAF");
        ctx.SendGroupJsonMessage         = (xlz::Fn_SendGroupJsonMessage)extractAddr(apiData, "\xE5\x8F\x91\xE9\x80\x81\xE7\xBE\xA4\x6A\x73\x6F\x6E\xE6\xB6\x88\xE6\x81\xAF");
        ctx.MuteGroupMember              = (xlz::Fn_MuteGroupMember)extractAddr(apiData, "\xE7\xA6\x81\xE8\xA8\x80\xE7\xBE\xA4\xE6\x88\x90\xE5\x91\x98");
        ctx.RemoveGroupMember            = (xlz::Fn_RemoveGroupMember)extractAddr(apiData, "\xE5\x88\xA0\xE9\x99\xA4\xE7\xBE\xA4\xE6\x88\x90\xE5\x91\x98");
        ctx.RecallGroupMessage           = (xlz::Fn_RecallGroupMessage)extractAddr(apiData, "\xE6\x92\xA4\xE5\x9B\x9E\xE6\xB6\x88\xE6\x81\xAF\x5F\xE7\xBE\xA4\xE8\x81\x8A");
        ctx.MuteAll                      = (xlz::Fn_MuteAll)extractAddr(apiData, "\xE5\x85\xA8\xE5\x91\x98\xE7\xA6\x81\xE8\xA8\x80");
        ctx.QQLike                       = (xlz::Fn_QQLike)extractAddr(apiData, "\x51\x51\xE7\x82\xB9\xE8\xB5\x9E");
        ctx.GetGroupCard                 = (xlz::Fn_GetGroupCard)extractAddr(apiData, "\xE5\x8F\x96\xE7\xBE\xA4\xE5\x90\x8D\xE7\x89\x87");
        ctx.SetGroupCard                 = (xlz::Fn_SetGroupCard)extractAddr(apiData, "\xE8\xAE\xBE\xE7\xBD\xAE\xE7\xBE\xA4\xE5\x90\x8D\xE7\x89\x87");
        ctx.GetNicknameForce             = (xlz::Fn_GetNicknameForce)extractAddr(apiData, "\xE5\xBC\xBA\xE5\x88\xB6\xE5\x8F\x96\xE6\x98\xB5\xE7\xA7\xB0");
        ctx.GetGroupMemberInfo           = (xlz::Fn_GetGroupMemberInfo)extractAddr(apiData, "\xE5\x8F\x96\xE7\xBE\xA4\xE6\x88\x90\xE5\x91\x98\xE4\xBF\xA1\xE6\x81\xAF");
        ctx.GetPluginDataDirectory       = (xlz::Fn_GetPluginDataDirectory)extractAddr(apiData, "\xE5\x8F\x96\xE6\x8F\x92\xE4\xBB\xB6\xE6\x95\xB0\xE6\x8D\xAE\xE7\x9B\xAE\xE5\xBD\x95");
        ctx.GetPluginSelfVersion         = (xlz::Fn_GetPluginSelfVersion)extractAddr(apiData, "\xE5\x8F\x96\xE6\x8F\x92\xE4\xBB\xB6\xE8\x87\xAA\xE8\xBA\xAB\xE7\x89\x88\xE6\x9C\xAC\xE5\x8F\xB7");
        ctx.GetFrameworkMainWindowHandle = (xlz::Fn_GetFrameworkMainWindowHandle)extractAddr(apiData, "\xE5\x8F\x96\xE6\xA1\x86\xE6\x9E\xB6\xE4\xB8\xBB\xE7\xAA\x97\xE5\x8F\xA3\xE5\x8F\xA5\xE6\x9F\x84");
        ctx.GetQQAvatar                  = (xlz::Fn_GetQQAvatar)extractAddr(apiData, "\xE5\x8F\x96\x51\x51\xE5\xA4\xB4\xE5\x83\x8F");
        ctx.GetPluginFileName            = (xlz::Fn_GetPluginFileName)extractAddr(apiData, "\xE5\x8F\x96\xE6\x8F\x92\xE4\xBB\xB6\xE6\x96\x87\xE4\xBB\xB6\xE5\x90\x8D");
        ctx.GetFrameworkVersion          = (xlz::Fn_GetFrameworkVersion)extractAddr(apiData, "\xE5\x8F\x96\xE6\xA1\x86\xE6\x9E\xB6\xE7\x89\x88\xE6\x9C\xAC");
        ctx.GetCurrentOneBotClientType   = (xlz::Fn_GetCurrentOneBotClientType)extractAddr(apiData, "\xE5\x8F\x96\xE5\xBD\x93\xE5\x89\x8DOneBot\xE5\xAE\xA2\xE6\x88\xB7\xE7\xAB\xAF\xE7\xB1\xBB\xE5\x9E\x8B");
        ctx.CallOneBotInterface          = (xlz::Fn_CallOneBotInterface)extractAddr(apiData, "\xE8\xB0\x83\xE7\x94\xA8\xE6\x8C\x87\xE5\xAE\x9A\x4F\x6E\x65\x42\x6F\x74\xE6\x8E\xA5\xE5\x8F\xA3");
        ctx.HandleFriendVerificationEvent = (xlz::Fn_HandleFriendVerificationEvent)extractAddr(apiData, "\xE5\xA4\x84\xE7\x90\x86\xE5\xA5\xBD\xE5\x8F\x8B\xE9\xAA\x8C\xE8\xAF\x81\xE4\xBA\x8B\xE4\xBB\xB6");
        ctx.HandleGroupVerificationEvent = (xlz::Fn_HandleGroupVerificationEvent)extractAddr(apiData, "\xE5\xA4\x84\xE7\x90\x86\xE7\xBE\xA4\xE9\xAA\x8C\xE8\xAF\x81\xE4\xBA\x8B\xE4\xBB\xB6");
        ctx.GetAdministratorList         = (xlz::Fn_GetAdministratorList)extractAddr(apiData, "\xE5\x8F\x96\xE7\xAE\xA1\xE7\x90\x86\xE5\xB1\x82\xE5\x88\x97\xE8\xA1\xA8");
        ctx.ReloadItSelf                 = (xlz::Fn_ReloadItSelf)extractAddr(apiData, "\xE9\x87\x8D\xE8\xBD\xBD\xE8\x87\xAA\xE8\xBA\xAB");
        ctx.UploadFriendImage            = (xlz::Fn_UploadFriendImage)extractAddr(apiData, "\xE4\xB8\x8A\xE4\xBC\xA0\xE5\xA5\xBD\xE5\x8F\x8B\xE5\x9B\xBE\xE7\x89\x87");
        ctx.UploadGroupImage             = (xlz::Fn_UploadGroupImage)extractAddr(apiData, "\xE4\xB8\x8A\xE4\xBC\xA0\xE7\xBE\xA4\xE5\x9B\xBE\xE7\x89\x87");
    }
    
    // Return AppInfo JSON (same format as original XiaoLiZi SDK)
    g_appInfoJson = "{";
    g_appInfoJson += "\"name\":\"" + std::string(g_appName) + "\",";
    g_appInfoJson += "\"version\":\"" + std::string(g_appVersion) + "\",";
    g_appInfoJson += "\"author\":\"" + std::string(g_appAuthor) + "\",";
    g_appInfoJson += "\"description\":\"" + std::string(g_appDesc) + "\",";
    g_appInfoJson += "\"groupmsaddres\":" + std::to_string((uintptr_t)&OnGroupMessage) + ",";
    g_appInfoJson += "\"friendmsaddres\":" + std::to_string((uintptr_t)&OnPrivateMessage) + ",";
    g_appInfoJson += "\"eventmsaddres\":" + std::to_string((uintptr_t)&OnEvent) + ",";
    g_appInfoJson += "\"useproaddres\":" + std::to_string((uintptr_t)&OnPluginEnable) + ",";
    g_appInfoJson += "\"banproaddres\":" + std::to_string((uintptr_t)&OnPluginDisable) + ",";
    g_appInfoJson += "\"unitproaddres\":" + std::to_string((uintptr_t)&OnPluginUninstall) + ",";
    g_appInfoJson += "\"setproaddres\":" + std::to_string((uintptr_t)&OnAppSetting);
    g_appInfoJson += "}";
    
    return g_appInfoJson.c_str();
}

// Standard XiaoLiZi callbacks
XLZ_API int XLZ_CALL RecviceGroupMesg(void* data) {
    return OnGroupMessage(static_cast<const xlz::GroupMessageEvent*>(data));
}

XLZ_API int XLZ_CALL RecvicePrivateMsg(void* data) {
    return OnPrivateMessage(static_cast<const xlz::PrivateMessageEvent*>(data));
}

XLZ_API int XLZ_CALL RecviceEventCallBack(void* data) {
    return OnEvent(static_cast<const xlz::EventTypeBase*>(data));
}

XLZ_API int XLZ_CALL RotbotAppEnable() {
    return OnPluginEnable();
}

XLZ_API void XLZ_CALL AppUninstall() {
    OnPluginUninstall();
}

XLZ_API void XLZ_CALL AppDisabled() {
    OnPluginDisable();
}

XLZ_API int XLZ_CALL AppSetting() {
    return OnAppSetting();
}

// xlz_x86_host.cpp - 32-bit XiaoLiZi plugin host for YuexBot.
// Keep this host small: it runs x86/易语言 DLLs out-of-process and talks to
// YuexBot through line-delimited JSON on stdin/stdout.

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "sdk/xlz_compat_sdk.h"

static std::string g_lastText;
static std::string g_capAppName, g_capAppAuthor, g_capAppVersion, g_capAppDesc;

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

static std::wstring ansi_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), NULL, 0);
    if (n <= 0) return L"";
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), &out[0], n);
    return out;
}

static std::string wide_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), &out[0], n, NULL, NULL);
    return out;
}

static std::string wide_to_ansi_bytes(const std::wstring& ws) {
    if (ws.empty()) return "";
    int n = WideCharToMultiByte(936, 0, ws.data(), (int)ws.size(), NULL, 0, NULL, NULL);
    if (n <= 0) return "";
    std::string out(n, '\0');
    WideCharToMultiByte(936, 0, ws.data(), (int)ws.size(), &out[0], n, NULL, NULL);
    return out;
}

static bool is_valid_utf8(const std::string& s) {
    int need = 0;
    for (unsigned char c : s) {
        if (!need) {
            if ((c >> 7) == 0) continue;
            if ((c >> 5) == 0x6) need = 1;
            else if ((c >> 4) == 0xE) need = 2;
            else if ((c >> 3) == 0x1E) need = 3;
            else return false;
        } else {
            if ((c >> 6) != 0x2) return false;
            --need;
        }
    }
    return need == 0;
}

static std::string repair_utf8_mojibake_from_ansi(const std::string& utf8Text) {
    std::wstring w = utf8_to_wide(utf8Text);
    std::string ansiBytes = wide_to_ansi_bytes(w);
    if (!ansiBytes.empty() && ansiBytes != utf8Text && is_valid_utf8(ansiBytes)) return ansiBytes;
    return utf8Text;
}

static std::string norm(const std::string& s) {
    if (s.empty()) return "";
    if (is_valid_utf8(s)) return repair_utf8_mojibake_from_ansi(s);
    return repair_utf8_mojibake_from_ansi(wide_to_utf8(ansi_to_wide(s)));
}

static std::string basename_no_ext(std::string p) {
    size_t slash = p.find_last_of("\\/");
    if (slash != std::string::npos) p = p.substr(slash + 1);
    size_t dot = p.find_last_of('.');
    if (dot != std::string::npos) p.resize(dot);
    return norm(p);
}

static std::string dirname(const std::string& p) {
    size_t slash = p.find_last_of("\\/");
    return slash == std::string::npos ? std::string() : p.substr(0, slash);
}

static std::string absolute_path_utf8(const std::string& path) {
    std::wstring w = utf8_to_wide(path);
    if (w.empty()) return path;
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetFullPathNameW(w.c_str(), MAX_PATH, buf, NULL);
    if (n == 0 || n >= MAX_PATH) return path;
    return wide_to_utf8(std::wstring(buf));
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 0x20) out += " ";
        else out.push_back((char)c);
    }
    return out;
}

static void emit_raw(const std::string& json) {
    std::cout << json << std::endl;
    std::cout.flush();
}

static void emit_event(const std::string& ev, const std::string& body) {
    emit_raw("{\"event\":\"" + json_escape(ev) + "\"" + body + "}");
}

static std::string win_error_message(DWORD err) {
    if (err == ERROR_MOD_NOT_FOUND) return "missing dependency DLL or runtime, code 126";
    if (err == ERROR_BAD_EXE_FORMAT) return "DLL architecture or format mismatch, code 193";
    if (err == ERROR_DLL_INIT_FAILED) return "DLL initialization failed, code 1114";
    return "LoadLibrary failed, code " + std::to_string((unsigned long)err);
}

static std::string find_json_string(const std::string& s, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = s.find(needle);
    if (p == std::string::npos) return "";
    p = s.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    p = s.find('"', p + 1);
    if (p == std::string::npos) return "";
    std::string out;
    bool esc = false;
    for (++p; p < s.size(); ++p) {
        char c = s[p];
        if (esc) {
            if (c == 'n') out.push_back('\n');
            else if (c == 'r') out.push_back('\r');
            else if (c == 't') out.push_back('\t');
            else out.push_back(c);
            esc = false;
            continue;
        }
        if (c == '\\') { esc = true; continue; }
        if (c == '"') break;
        out.push_back(c);
    }
    return norm(out);
}

static int64_t find_json_i64(const std::string& s, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = s.find(needle);
    if (p == std::string::npos) return 0;
    p = s.find(':', p + needle.size());
    if (p == std::string::npos) return 0;
    ++p;
    while (p < s.size() && (s[p] == ' ' || s[p] == '"')) ++p;
    return _strtoi64(s.c_str() + p, NULL, 10);
}

static std::string extract_event_object(const std::string& line) {
    std::string needle = "\"event\"";
    size_t key = line.find(needle);
    if (key == std::string::npos) return "";
    size_t colon = line.find(':', key + needle.size());
    if (colon == std::string::npos) return "";
    size_t start = line.find('{', colon + 1);
    if (start == std::string::npos) return "";
    int depth = 0;
    bool inStr = false, esc = false;
    for (size_t i = start; i < line.size(); ++i) {
        char c = line[i];
        if (inStr) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') { inStr = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}') {
            --depth;
            if (depth == 0) return line.substr(start, i - start + 1);
        }
    }
    return "";
}

typedef int (XLZ_CALL *FnInt0)();
typedef void (XLZ_CALL *FnVoid0)();
typedef int (XLZ_CALL *FnMsg)(void*);
typedef const char* (XLZ_CALL *FnAppRun)(const char*, const char*);
typedef int (XLZ_CALL *FnInit2)(const char*, const char*);
typedef int (XLZ_CALL *FnInit3)(const char*, const char*, const char*);
typedef int (XLZ_CALL *FnOnGroup16)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const char*, const char*, const char*, const char*);
typedef int (XLZ_CALL *FnOnFriend17)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const char*, const char*, const char*);
typedef int (XLZ_CALL *FnOnEvent22)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const char*, const char*, const char*, const char*, const char*);

template <typename T>
static T gp(HMODULE m, const char* name) { return m ? (T)GetProcAddress(m, name) : nullptr; }

template <typename T>
static T gp2(HMODULE m, const char* a, const char* b) {
    if (!m) return nullptr;
    FARPROC p = GetProcAddress(m, a);
    if (!p && b) p = GetProcAddress(m, b);
    return (T)p;
}

static int safe_int(FnInt0 fn, const char* label) {
    if (!fn) return 0;
    try { return fn(); } catch (...) { emit_event("error", ",\"error\":\"" + std::string(label) + " exception\""); return 0; }
}

static void safe_void(FnVoid0 fn, const char* label) {
    if (!fn) return;
    try { fn(); } catch (...) { emit_event("error", ",\"error\":\"" + std::string(label) + " exception\""); }
}

static int safe_msg(FnMsg fn, void* data, const char* label) {
    if (!fn) return 0;
    try { return fn(data); } catch (...) { emit_event("error", ",\"error\":\"" + std::string(label) + " exception\""); return 0; }
}

static int safe_init2(FnInit2 fn, const char* pluginKey, const char* apiData, const char* label) {
    if (!fn) return 0;
    try { return fn(pluginKey, apiData); } catch (...) { emit_event("error", ",\"error\":\"" + std::string(label) + " exception\""); return 0; }
}

static int safe_init3(FnInit3 fn, const char* pluginKey, const char* apiData, const char* dataDir, const char* label) {
    if (!fn) return 0;
    try { return fn(pluginKey, apiData, dataDir); } catch (...) { emit_event("error", ",\"error\":\"" + std::string(label) + " exception\""); return 0; }
}

static int safe_group16(FnOnGroup16 fn, uint32_t self, uint32_t group, uint32_t user, uint32_t req, uint32_t tm, uint32_t random, uint32_t clip, uint32_t clipCount, uint32_t clipId, uint32_t msgType, uint32_t bubble, uint32_t level, const char* groupName, const char* senderName, const char* title, const char* content) {
    if (!fn) return 0;
    try { return fn(self, group, user, req, tm, random, clip, clipCount, clipId, msgType, bubble, level, groupName, senderName, title, content); } catch (...) { emit_event("error", ",\"error\":\"onGroupMsg exception\""); return 0; }
}

static int safe_friend17(FnOnFriend17 fn, uint32_t self, uint32_t user, uint32_t req, uint32_t seq, uint32_t tm, uint32_t random, uint32_t clip, uint32_t clipCount, uint32_t clipId, uint32_t msgType, uint32_t subType, uint32_t tempType, uint32_t redType, uint32_t sourceQQ, const char* senderName, const char* content, const char* fileName) {
    if (!fn) return 0;
    try { return fn(self, user, req, seq, tm, random, clip, clipCount, clipId, msgType, subType, tempType, redType, sourceQQ, senderName, content, fileName); } catch (...) { emit_event("error", ",\"error\":\"onFriendMsg exception\""); return 0; }
}

static int safe_event22(FnOnEvent22 fn, uint32_t self, uint32_t group, uint32_t op, uint32_t trigger, uint32_t seq, uint32_t tm, uint32_t eventType, uint32_t subType, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g, uint32_t h, uint32_t i, const char* groupName, const char* opName, const char* triggerName, const char* content, const char* extra) {
    if (!fn) return 0;
    try { return fn(self, group, op, trigger, seq, tm, eventType, subType, a, b, c, d, e, f, g, h, i, groupName, opName, triggerName, content, extra); } catch (...) { emit_event("error", ",\"error\":\"onEventMsg exception\""); return 0; }
}

static const char* XLZ_CALL host_output_log(const char* pluginKey, const char* msg, uint32_t fg, uint32_t bg) {
    (void)fg; (void)bg;
    emit_event("plugin_log", ",\"plugin\":\"" + json_escape(norm(pluginKey ? pluginKey : "")) + "\",\"message\":\"" + json_escape(norm(msg ? msg : "")) + "\"");
    g_lastText = "{\"status\":\"ok\",\"retcode\":0}";
    return g_lastText.c_str();
}

static const char* XLZ_CALL host_send_friend_message(const char* pluginKey, uint64_t thisQQ, uint64_t friendQQ, const char* msg, int64_t* outRandom, uint32_t* outReq) {
    if (outRandom) *outRandom = 0;
    if (outReq) *outReq = 0;
    emit_event("send_private_msg", ",\"plugin\":\"" + json_escape(norm(pluginKey ? pluginKey : "")) + "\",\"self_id\":" + std::to_string(thisQQ) + ",\"user_id\":" + std::to_string(friendQQ) + ",\"message\":\"" + json_escape(norm(msg ? msg : "")) + "\"");
    g_lastText = "{\"status\":\"pending\",\"retcode\":0}";
    return g_lastText.c_str();
}

static const char* XLZ_CALL host_send_group_message(const char* pluginKey, uint64_t thisQQ, uint64_t groupQQ, const char* msg, uint32_t anonymous) {
    (void)anonymous;
    emit_event("send_group_msg", ",\"plugin\":\"" + json_escape(norm(pluginKey ? pluginKey : "")) + "\",\"self_id\":" + std::to_string(thisQQ) + ",\"group_id\":" + std::to_string(groupQQ) + ",\"message\":\"" + json_escape(norm(msg ? msg : "")) + "\"");
    g_lastText = "{\"status\":\"pending\",\"retcode\":0}";
    return g_lastText.c_str();
}

static const char* XLZ_CALL host_get_framework_qq(const char* pluginKey) {
    (void)pluginKey;
    g_lastText = "{\"status\":\"ok\",\"retcode\":0,\"data\":{\"user_id\":0,\"nickname\":\"YuexBot x86 Host\"}}";
    return g_lastText.c_str();
}

static uint32_t XLZ_CALL host_zero() { return 0; }
static const char* XLZ_CALL host_text(const char* pluginKey) { (void)pluginKey; g_lastText = "YuexBot x86 Host"; return g_lastText.c_str(); }
static const char* XLZ_CALL host_avatar(const char* pluginKey, uint64_t otherQQ, uint32_t hd) { (void)pluginKey; (void)hd; g_lastText = "http://q1.qlogo.cn/g?b=qq&nk=" + std::to_string(otherQQ) + "&s=100"; return g_lastText.c_str(); }
static uint32_t XLZ_CALL host_ok5(const char* pluginKey, uint64_t a, uint64_t b, uint64_t c, uint32_t d) { (void)pluginKey; (void)a; (void)b; (void)c; (void)d; return 1; }
static uint32_t XLZ_CALL host_verify(const char* pluginKey, uint64_t a, uint64_t b, uint32_t c, const char* d, uint32_t accept) { (void)pluginKey; (void)a; (void)b; (void)c; (void)d; return accept ? 1 : 0; }
static uint32_t XLZ_CALL host_group_verify(const char* pluginKey, uint64_t a, uint64_t b, uint64_t c, uint32_t d, const char* e, uint32_t accept) { (void)pluginKey; (void)a; (void)b; (void)c; (void)d; (void)e; return accept ? 1 : 0; }
static void XLZ_CALL host_reload(const char* pluginKey, const char* dllPath) { (void)pluginKey; (void)dllPath; }

static const char* XLZ_CALL host_set_app_name(const char* pluginKey, const char* value) { (void)pluginKey; if (value) g_capAppName = value; g_lastText.clear(); return g_lastText.c_str(); }
static const char* XLZ_CALL host_set_app_author(const char* pluginKey, const char* value) { (void)pluginKey; if (value) g_capAppAuthor = value; g_lastText.clear(); return g_lastText.c_str(); }
static const char* XLZ_CALL host_set_app_version(const char* pluginKey, const char* value) { (void)pluginKey; if (value) g_capAppVersion = value; g_lastText.clear(); return g_lastText.c_str(); }
static const char* XLZ_CALL host_set_app_desc(const char* pluginKey, const char* value) { (void)pluginKey; if (value) g_capAppDesc = value; g_lastText.clear(); return g_lastText.c_str(); }

static const char* XLZ_CALL host_call_onebot(const char* pluginKey, uint64_t thisQQ, const char* sendData, uint32_t noWait) {
    (void)noWait;
    emit_event("onebot_call", ",\"plugin\":\"" + json_escape(norm(pluginKey ? pluginKey : "")) + "\",\"self_id\":" + std::to_string(thisQQ) + ",\"request\":\"" + json_escape(norm(sendData ? sendData : "")) + "\"");
    g_lastText = "{\"status\":\"pending\",\"retcode\":0}";
    return g_lastText.c_str();
}

static std::string build_apidata() {
    std::string j = "{";
    auto add = [&](const char* k, uintptr_t v) {
        if (j.size() > 1) j += ",";
        j += "\"" + json_escape(k) + "\":" + std::to_string((uint64_t)v);
    };
    add(xlz::kApi_OutputLog, (uintptr_t)&host_output_log);
    add(xlz::kApi_SendFriendMessage, (uintptr_t)&host_send_friend_message);
    add(xlz::kApi_SendGroupMessage, (uintptr_t)&host_send_group_message);
    add(xlz::kApi_GetFrameworkQQ, (uintptr_t)&host_get_framework_qq);
    add(xlz::kApi_GetGroupList, (uintptr_t)&host_zero);
    add(xlz::kApi_GetGroupMemberList, (uintptr_t)&host_zero);
    add(xlz::kApi_SendGroupTemporaryMsg, (uintptr_t)&host_send_friend_message);
    add(xlz::kApi_SendGroupJsonMessage, (uintptr_t)&host_send_group_message);
    add(xlz::kApi_MuteGroupMember, (uintptr_t)&host_ok5);
    add(xlz::kApi_RemoveGroupMember, (uintptr_t)&host_ok5);
    add(xlz::kApi_RecallGroupMessage, (uintptr_t)&host_ok5);
    add(xlz::kApi_MuteAll, (uintptr_t)&host_ok5);
    add(xlz::kApi_QQLike, (uintptr_t)&host_ok5);
    add(xlz::kApi_GetGroupCard, (uintptr_t)&host_text);
    add(xlz::kApi_SetGroupCard, (uintptr_t)&host_ok5);
    add(xlz::kApi_GetNicknameForce, (uintptr_t)&host_text);
    add(xlz::kApi_GetGroupMemberInfo, (uintptr_t)&host_text);
    add(xlz::kApi_GetPluginDataDir, (uintptr_t)&host_text);
    add(xlz::kApi_GetPluginSelfVersion, (uintptr_t)&host_text);
    add(xlz::kApi_GetFrameworkMainWnd, (uintptr_t)&host_zero);
    add(xlz::kApi_GetQQAvatar, (uintptr_t)&host_avatar);
    add(xlz::kApi_GetPluginFileName, (uintptr_t)&host_text);
    add(xlz::kApi_GetFrameworkVersion, (uintptr_t)&host_text);
    add(xlz::kApi_GetCurrentOBType, (uintptr_t)&host_text);
    add(xlz::kApi_CallOneBotInterface, (uintptr_t)&host_call_onebot);
    add(xlz::kApi_HandleFriendVerify, (uintptr_t)&host_verify);
    add(xlz::kApi_HandleGroupVerify, (uintptr_t)&host_group_verify);
    add(xlz::kApi_GetAdminList, (uintptr_t)&host_text);
    add(xlz::kApi_ReloadItSelf, (uintptr_t)&host_reload);
    add(xlz::kApi_UploadFriendImage, (uintptr_t)&host_text);
    add(xlz::kApi_UploadGroupImage, (uintptr_t)&host_text);
    add(xlz::kApi_SetAppName, (uintptr_t)&host_set_app_name);
    add(xlz::kApi_SetAppAuthor, (uintptr_t)&host_set_app_author);
    add(xlz::kApi_SetAppVersion, (uintptr_t)&host_set_app_version);
    add(xlz::kApi_SetAppDescription, (uintptr_t)&host_set_app_desc);
    // GBK-encoded duplicate keys: e-language XLZ SDK is GBK-native and may look up callbacks by GBK byte strings.
    add("\xD6\xC3\xD3\xA6\xD3\xC3\xC3\xFB", (uintptr_t)&host_set_app_name);          // 置应用名
    add("\xD6\xC3\xD3\xA6\xD3\xC3\xD7\xF7\xD5\xDF", (uintptr_t)&host_set_app_author); // 置应用作者
    add("\xD6\xC3\xD3\xA6\xD3\xC3\xB0\xE6\xB1\xBE", (uintptr_t)&host_set_app_version);// 置应用版本
    add("\xD6\xC3\xD3\xA6\xD3\xC3\xCB\xB5\xC3\xF7", (uintptr_t)&host_set_app_desc);   // 置应用说明
    add("\xCA\xE4\xB3\xF6\xC8\xD5\xD6\xBE", (uintptr_t)&host_output_log);             // 输出日志 (GBK probe)
    j += "}";
    return j;
}

static uintptr_t app_addr(const std::string& app, const char* key) { return (uintptr_t)find_json_i64(app, key); }

// Read a small UTF-8 text file (used for plugin metadata sidecars).
static std::string read_text_file(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return "";
    std::string out;
    char buf[4096];
    DWORD rd = 0;
    while (ReadFile(h, buf, sizeof(buf), &rd, NULL) && rd > 0) out.append(buf, rd);
    CloseHandle(h);
    // Strip UTF-8 BOM if present.
    if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF)
        out.erase(0, 3);
    return out;
}

// Look for a metadata sidecar next to the plugin DLL: <dll>.info.json, <stem>.info.json or <stem>.json.
static std::string read_sidecar_metadata(const std::string& dllPath) {
    std::string dir = dirname(dllPath);
    std::string stem = basename_no_ext(dllPath);
    std::string base = dir.empty() ? stem : (dir + "\\" + stem);
    const char* exts[] = { ".info.json", ".meta.json", ".json" };
    for (const char* ext : exts) {
        std::string p = base + ext;
        std::string content = read_text_file(utf8_to_wide(p));
        if (!content.empty()) return content;
    }
    return "";
}

// Read author/version/etc from the PE version resource (works for C++ DLLs that embed one).
static void read_pe_version_metadata(const std::string& dllPath, std::string& outVersion, std::string& outAuthor, std::string& outName, std::string& outDesc) {
    std::wstring wpath = utf8_to_wide(dllPath);
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(wpath.c_str(), &dummy);
    if (!size) return;
    std::vector<char> data(size);
    if (!GetFileVersionInfoW(wpath.c_str(), 0, size, data.data())) return;
    struct LangCp { WORD lang; WORD cp; };
    LangCp* langs = nullptr; UINT langBytes = 0;
    VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&langs, &langBytes);
    int count = langBytes / sizeof(LangCp);
    auto query = [&](const wchar_t* field) -> std::string {
        for (int i = 0; i < count; ++i) {
            wchar_t sub[128];
            wsprintfW(sub, L"\\StringFileInfo\\%04x%04x\\%s", langs[i].lang, langs[i].cp, field);
            wchar_t* val = nullptr; UINT len = 0;
            if (VerQueryValueW(data.data(), sub, (LPVOID*)&val, &len) && val && len > 1)
                return norm(wide_to_utf8(val));
        }
        return "";
    };
    if (count > 0) {
        std::string fv = query(L"FileVersion");
        std::string pv = query(L"ProductVersion");
        std::string company = query(L"CompanyName");
        std::string product = query(L"ProductName");
        std::string desc = query(L"FileDescription");
        if (outVersion.empty()) outVersion = !fv.empty() ? fv : pv;
        if (outAuthor.empty()) outAuthor = company;
        if (outName.empty()) outName = product;
        if (outDesc.empty()) outDesc = desc;
    }
}

static std::string build_fallback_appinfo(const std::string& pluginKey, const std::string& pluginType, const std::string& description) {
    std::string j = "{";
    j += "\"name\":\"" + json_escape(norm(pluginKey)) + "\"";
    j += ",\"version\":\"\"";
    j += ",\"author\":\"\"";
    j += ",\"plugin_type\":\"" + json_escape(pluginType) + "\"";
    j += ",\"description\":\"" + json_escape(description) + "\"";
    j += "}";
    return j;
}

// Build appinfo from metadata the plugin registered via SetApp* callbacks during AppStart.
static std::string build_captured_appinfo(const std::string& pluginKey, const std::string& pluginType, const std::string& fallbackDesc, const std::string& dllPath) {
    // Priority: metadata pushed by plugin via SetApp* callbacks > sidecar json > PE version resource > filename fallback.
    std::string name = g_capAppName, version = g_capAppVersion, author = g_capAppAuthor, desc = g_capAppDesc, sdkVersion;
    std::string sidecar = read_sidecar_metadata(dllPath);
    if (!sidecar.empty()) {
        if (name.empty())    name    = find_json_string(sidecar, "name");
        if (name.empty())    name    = find_json_string(sidecar, "appname");
        if (version.empty()) version = find_json_string(sidecar, "version");
        if (version.empty()) version = find_json_string(sidecar, "appv");
        if (author.empty())  author  = find_json_string(sidecar, "author");
        if (desc.empty())    desc    = find_json_string(sidecar, "description");
        if (desc.empty())    desc    = find_json_string(sidecar, "desc");
        if (sdkVersion.empty()) sdkVersion = find_json_string(sidecar, "sdkv");
        if (sdkVersion.empty()) sdkVersion = find_json_string(sidecar, "sdk_version");
    }
    read_pe_version_metadata(dllPath, version, author, name, desc);
    if (name.empty()) name = pluginKey;
    if (desc.empty()) desc = fallbackDesc;
    std::string j = "{";
    j += "\"name\":\"" + json_escape(norm(name)) + "\"";
    j += ",\"version\":\"" + json_escape(norm(version)) + "\"";
    j += ",\"author\":\"" + json_escape(norm(author)) + "\"";
    if (!sdkVersion.empty()) j += ",\"sdkv\":\"" + json_escape(norm(sdkVersion)) + "\"";
    j += ",\"plugin_type\":\"" + json_escape(pluginType) + "\"";
    j += ",\"description\":\"" + json_escape(norm(desc)) + "\"";
    j += "}";
    return j;
}

int wmain(int argc, wchar_t** wargv) {
    SetConsoleOutputCP(CP_UTF8);
    bool serve = false;
    int base = 1;
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) args.push_back(wide_to_utf8(wargv && wargv[i] ? wargv[i] : L""));
    if (argc >= 2 && args[1] == "--serve") { serve = true; base = 2; }
    if (argc <= base) { emit_raw("{\"ok\":false,\"error\":\"missing dll path\"}"); return 2; }

    std::string dllPath = args[base];
    dllPath = absolute_path_utf8(dllPath);
    std::string pluginKey = argc > base + 1 ? args[base + 1] : basename_no_ext(dllPath);
    emit_event("phase", ",\"phase\":\"host_start\",\"dll\":\"" + json_escape(norm(dllPath)) + "\"");

    std::string pdir = dirname(dllPath);
    char exePath[MAX_PATH] = {};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string bdir = dirname(exePath);
    std::string mdir = dirname(bdir);
    if (!pdir.empty()) SetDllDirectoryA(pdir.c_str());
    if (!bdir.empty()) AddDllDirectory(utf8_to_wide(bdir).c_str());
    if (!mdir.empty()) AddDllDirectory(utf8_to_wide(mdir).c_str());
    if (!pdir.empty()) AddDllDirectory(utf8_to_wide(pdir).c_str());

    std::wstring wdll = utf8_to_wide(dllPath);
    HMODULE mod = !wdll.empty() ? LoadLibraryExW(wdll.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH) : NULL;
    DWORD firstErr = mod ? 0 : GetLastError();
    if (!mod) {
        std::wstring adll = ansi_to_wide(dllPath);
        if (!adll.empty()) mod = LoadLibraryExW(adll.c_str(), NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
    }
    if (!mod) mod = LoadLibraryA(dllPath.c_str());
    if (!mod) {
        DWORD err = GetLastError();
        emit_event("error", ",\"ok\":false,\"error\":\"" + json_escape(win_error_message(err)) + "\",\"code\":" + std::to_string((int)err) + ",\"first_code\":" + std::to_string((int)firstErr));
        return 3;
    }

    FnAppRun apprun = gp2<FnAppRun>(mod, "apprun", "_apprun@8");
    FnMsg fnGroup = gp2<FnMsg>(mod, "RecviceGroupMesg", "_RecviceGroupMesg@4");
    FnMsg fnPrivate = gp2<FnMsg>(mod, "RecvicePrivateMsg", "_RecvicePrivateMsg@4");
    FnMsg fnEvent = gp2<FnMsg>(mod, "RecviceEventCallBack", "_RecviceEventCallBack@4");
    FnInt0 fnEnable = gp2<FnInt0>(mod, "RotbotAppEnable", "_RotbotAppEnable@0");
    FnVoid0 fnDisable = gp2<FnVoid0>(mod, "AppDisabled", "_AppDisabled@0");
    FnInt0 fnSettings = gp2<FnInt0>(mod, "AppSetting", "_AppSetting@0");
    FnInit3 fnInit3 = gp<FnInit3>(mod, "Init");
    FnOnGroup16 fnOnGroup = gp<FnOnGroup16>(mod, "onGroupMsg");
    FnOnFriend17 fnOnFriend = gp<FnOnFriend17>(mod, "onFriendMsg");
    FnOnEvent22 fnOnEvent = gp<FnOnEvent22>(mod, "onEventMsg");
    FnInt0 fnAppStart = gp<FnInt0>(mod, "_AppStart");
    FnVoid0 fnAppEnd = gp<FnVoid0>(mod, "_AppEnd");
    FnVoid0 fnAppUnload = gp<FnVoid0>(mod, "_AppUnload");
    FnInt0 fnControlPanel = gp<FnInt0>(mod, "_ControlPanel");
    FnMsg fnEOnGroup = gp<FnMsg>(mod, "_OnGroup");
    FnMsg fnEOnPrivate = gp<FnMsg>(mod, "_OnPrivate");
    FnMsg fnEOnEvent = gp<FnMsg>(mod, "_OnEvent");
    FnInit2 fnEInit2 = (FnInit2)GetProcAddress(mod, (LPCSTR)MAKEINTRESOURCEA(12));
    bool eAppStartMode = false;
    bool genericInitOnMode = false;
    int initResult = 0;

    if (!apprun) {
        if (!fnEnable) fnEnable = gp<FnInt0>(mod, "Enable");
        if (!fnDisable) fnDisable = gp<FnVoid0>(mod, "Disable");
        if (!fnDisable) fnDisable = gp<FnVoid0>(mod, "Uninit");
        if (!fnSettings) fnSettings = gp<FnInt0>(mod, "Setting");
        eAppStartMode = fnAppStart || fnEOnGroup || fnEOnPrivate || fnEOnEvent || fnControlPanel;
        if (eAppStartMode) {
            if (!fnEnable) fnEnable = fnAppStart;
            if (!fnDisable) fnDisable = fnAppEnd ? fnAppEnd : fnAppUnload;
            if (!fnSettings) fnSettings = fnControlPanel;
            if (!fnGroup) fnGroup = fnEOnGroup;
            if (!fnPrivate) fnPrivate = fnEOnPrivate;
            if (!fnEvent) fnEvent = fnEOnEvent;
            std::string dataDir = pdir.empty() ? (pluginKey + "_data") : (pdir + "\\" + pluginKey + "_data");
            CreateDirectoryA(dataDir.c_str(), NULL);
            initResult = safe_init2(fnEInit2, pluginKey.c_str(), build_apidata().c_str(), "E-language init callback");
            int startResult = safe_int(fnAppStart, "_AppStart callback");
            std::string appInfo = build_captured_appinfo(pluginKey, "xiaolizi-e-lang-appstart", "E-language XLZ SDK plugin (AppStart export mode)", dllPath);
            emit_event(serve ? "ready" : "loaded", std::string(",\"ok\":true,\"plugin_key\":\"") + json_escape(pluginKey) + "\",\"dll\":\"" + json_escape(norm(dllPath)) + "\",\"plugin_type\":\"xiaolizi-e-lang-appstart\",\"app_info_raw\":\"" + json_escape(appInfo) + "\",\"enabled\":true,\"init_result\":" + std::to_string(initResult) + ",\"start_result\":" + std::to_string(startResult) + ",\"enable_deferred\":false,\"has_group_callback\":" + (fnGroup ? "true" : "false") + ",\"has_private_callback\":" + (fnPrivate ? "true" : "false") + ",\"has_event_callback\":" + (fnEvent ? "true" : "false") + ",\"has_settings\":" + (fnSettings ? "true" : "false"));
        }
        genericInitOnMode = !eAppStartMode && (fnInit3 || fnOnGroup || fnOnFriend || fnOnEvent || fnEnable || fnSettings);
        if (!eAppStartMode && !genericInitOnMode) {
            bool genericOther = GetProcAddress(mod, "Lite_Init") || GetProcAddress(mod, "MQ_Info") || GetProcAddress(mod, "EB_Info") || GetProcAddress(mod, "Turbo_Init") || GetProcAddress(mod, "Dr_Create");
            emit_event(serve ? "ready" : "loaded", std::string(",\"ok\":false,\"plugin_key\":\"") + json_escape(pluginKey) + "\",\"dll\":\"" + json_escape(norm(dllPath)) + "\",\"plugin_type\":\"" + (genericOther ? "generic-x86" : "unknown-x86") + "\",\"enabled\":false,\"warning\":\"missing apprun export and no supported Init/on* adapter was found\",\"app_info_raw\":\"\",\"has_group_callback\":false,\"has_private_callback\":false,\"has_event_callback\":false,\"has_settings\":false");
            FreeLibrary(mod);
            return genericOther ? 0 : 4;
        }
        if (genericInitOnMode) {
            std::string dataDir = pdir.empty() ? (pluginKey + "_data") : (pdir + "\\" + pluginKey + "_data");
            CreateDirectoryA(dataDir.c_str(), NULL);
            initResult = safe_init3(fnInit3, pluginKey.c_str(), build_apidata().c_str(), dataDir.c_str(), "Init callback");
            std::string appInfo = build_captured_appinfo(pluginKey, "xiaolizi-init-onmsg", "XLZ SDK plugin (Init/onGroupMsg export mode)", dllPath);
            emit_event(serve ? "ready" : "loaded", std::string(",\"ok\":true,\"plugin_key\":\"") + json_escape(pluginKey) + "\",\"dll\":\"" + json_escape(norm(dllPath)) + "\",\"plugin_type\":\"xiaolizi-init-onmsg\",\"app_info_raw\":\"" + json_escape(appInfo) + "\",\"enabled\":true,\"init_result\":" + std::to_string(initResult) + ",\"enable_deferred\":" + (fnEnable ? "true" : "false") + ",\"has_group_callback\":" + (fnOnGroup ? "true" : "false") + ",\"has_private_callback\":" + (fnOnFriend ? "true" : "false") + ",\"has_event_callback\":" + (fnOnEvent ? "true" : "false") + ",\"has_settings\":" + (fnSettings ? "true" : "false"));
        }
    }

    if (apprun) {
        const char* raw = NULL;
        try { raw = apprun(pluginKey.c_str(), build_apidata().c_str()); }
        catch (...) { emit_event("error", ",\"ok\":false,\"error\":\"apprun exception\""); FreeLibrary(mod); return 5; }
        std::string app = norm(raw ? raw : "");
        if (!fnGroup) fnGroup = (FnMsg)app_addr(app, "groupmsaddres");
        if (!fnPrivate) fnPrivate = (FnMsg)app_addr(app, "friendmsaddres");
        if (!fnEvent) fnEvent = (FnMsg)app_addr(app, "eventmsaddres");
        if (!fnEnable) fnEnable = (FnInt0)app_addr(app, "useproaddres");
        if (!fnDisable) fnDisable = (FnVoid0)app_addr(app, "banproaddres");
        if (!fnSettings) fnSettings = (FnInt0)app_addr(app, "setproaddres");

        emit_event(serve ? "ready" : "loaded", ",\"ok\":true,\"plugin_key\":\"" + json_escape(pluginKey) + "\",\"dll\":\"" + json_escape(norm(dllPath)) + "\",\"plugin_type\":\"xiaolizi-apprun\",\"app_info_raw\":\"" + json_escape(app) + "\",\"enabled\":true,\"enable_deferred\":" + (fnEnable ? std::string("true") : std::string("false")) + ",\"has_group_callback\":" + (fnGroup ? "true" : "false") + ",\"has_private_callback\":" + (fnPrivate ? "true" : "false") + ",\"has_event_callback\":" + (fnEvent ? "true" : "false") + ",\"has_settings\":" + (fnSettings ? "true" : "false"));
    }

    if (serve) {
        std::string line;
        while (std::getline(std::cin, line)) {
            std::string action = find_json_string(line, "action");
            if (action == "shutdown") { safe_void(fnDisable, "disable callback"); emit_event("stopped", ",\"ok\":true"); break; }
            if (action == "enable") { int result = safe_int(fnEnable, "enable callback"); bool ok = eAppStartMode ? (fnEnable != nullptr) : (result != 0); emit_event("enable_result", std::string(",\"ok\":") + (ok ? "true" : "false") + ",\"result\":" + std::to_string(result) + ",\"available\":" + (fnEnable ? "true" : "false") + (ok ? "" : ",\"error\":\"enable callback returned 0 or callback missing\"") ); continue; }
            if (action == "settings") { int result = safe_int(fnSettings, "settings callback"); bool ok = eAppStartMode ? (fnSettings != nullptr) : (result != 0); emit_event("settings_result", std::string(",\"ok\":") + (ok ? "true" : "false") + ",\"result\":" + std::to_string(result) + ",\"available\":" + (fnSettings ? "true" : "false")); continue; }
            if (action == "event") {
                std::string e = extract_event_object(line);
                std::string post = find_json_string(e, "post_type");
                std::string mt = find_json_string(e, "message_type");
                int ret = 0;
                if (genericInitOnMode && (post == "message" || post == "message_sent") && mt == "group" && fnOnGroup) {
                    std::string groupName = find_json_string(e, "group_name");
                    std::string senderName = find_json_string(e, "sender_name");
                    std::string content = find_json_string(e, "raw_message");
                    uint32_t self = (uint32_t)find_json_i64(e, "self_id");
                    uint32_t group = (uint32_t)find_json_i64(e, "group_id");
                    uint32_t user = (uint32_t)find_json_i64(e, "user_id");
                    uint32_t tm = (uint32_t)find_json_i64(e, "time");
                    uint32_t random = (uint32_t)find_json_i64(e, "message_id");
                    ret = safe_group16(fnOnGroup, self, group, user, 0, tm, random, 0, 0, 0, 0, 0, 0, groupName.c_str(), senderName.c_str(), "", content.c_str());
                } else if (genericInitOnMode && (post == "message" || post == "message_sent") && mt == "private" && fnOnFriend) {
                    std::string senderName = find_json_string(e, "sender_name");
                    std::string content = find_json_string(e, "raw_message");
                    uint32_t self = (uint32_t)find_json_i64(e, "self_id");
                    uint32_t user = (uint32_t)find_json_i64(e, "user_id");
                    uint32_t seq = (uint32_t)find_json_i64(e, "message_id");
                    uint32_t tm = (uint32_t)find_json_i64(e, "time");
                    ret = safe_friend17(fnOnFriend, self, user, 0, seq, tm, seq, 0, 0, 0, 0, 0, 0, 0, user, senderName.c_str(), content.c_str(), "");
                } else if (genericInitOnMode && fnOnEvent) {
                    std::string content = find_json_string(e, "notice_type");
                    if (content.empty()) content = find_json_string(e, "request_type");
                    std::string groupName = find_json_string(e, "group_name");
                    std::string senderName = find_json_string(e, "sender_name");
                    uint32_t self = (uint32_t)find_json_i64(e, "self_id");
                    uint32_t group = (uint32_t)find_json_i64(e, "group_id");
                    uint32_t op = (uint32_t)find_json_i64(e, "operator_id");
                    uint32_t user = (uint32_t)find_json_i64(e, "user_id");
                    uint32_t seq = (uint32_t)find_json_i64(e, "message_id");
                    uint32_t tm = (uint32_t)find_json_i64(e, "time");
                    ret = safe_event22(fnOnEvent, self, group, op, user, seq, tm, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, groupName.c_str(), "", senderName.c_str(), content.c_str(), e.c_str());
                } else 
                if ((post == "message" || post == "message_sent") && mt == "group" && fnGroup) {
                    xlz::GroupMessageEvent ge = {};
                    std::string groupName = find_json_string(e, "group_name");
                    std::string senderName = find_json_string(e, "sender_name");
                    std::string content = find_json_string(e, "raw_message");
                    ge.SenderQQ = find_json_i64(e, "user_id");
                    ge.ThisQQ = find_json_i64(e, "self_id");
                    ge.MessageGroupQQ = find_json_i64(e, "group_id");
                    ge.MessageReceiveTime = (int32_t)find_json_i64(e, "time");
                    ge.MessageSendTime = ge.MessageReceiveTime;
                    ge.MessageRandom = find_json_i64(e, "message_id");
                    ge.MessageContent = content.c_str();
                    ge.SourceGroupName = groupName.c_str();
                    ge.SenderNickname = senderName.c_str();
                    ge.SenderTitle = ""; ge.ReplyMessageContent = ""; ge.AnonymousNickname = ""; ge.ReservedParameters = "";
                    ret = safe_msg(fnGroup, &ge, "group message callback");
                } else if ((post == "message" || post == "message_sent") && mt == "private" && fnPrivate) {
                    xlz::PrivateMessageEvent pe = {};
                    std::string content = find_json_string(e, "raw_message");
                    std::string senderName = find_json_string(e, "sender_name");
                    pe.SenderQQ = find_json_i64(e, "user_id");
                    pe.ThisQQ = find_json_i64(e, "self_id");
                    pe.MessageSeq = find_json_i64(e, "message_id");
                    pe.MessageReceiveTime = (uint32_t)find_json_i64(e, "time");
                    pe.MessageSendTime = pe.MessageReceiveTime;
                    pe.MessageRandom = pe.MessageSeq;
                    pe.MessageContent = content.c_str();
                    pe.SourceEventQQName = senderName.c_str();
                    pe.FileID = ""; pe.FileMD5 = ""; pe.FileName = "";
                    ret = safe_msg(fnPrivate, &pe, "private message callback");
                } else if (fnEvent) {
                    xlz::EventTypeBase eb = {};
                    std::string content = find_json_string(e, "notice_type");
                    std::string groupName = find_json_string(e, "group_name");
                    std::string senderName = find_json_string(e, "sender_name");
                    eb.ThisQQ = find_json_i64(e, "self_id");
                    eb.SourceGroupQQ = find_json_i64(e, "group_id");
                    eb.OperateQQ = find_json_i64(e, "operator_id");
                    eb.TriggerQQ = find_json_i64(e, "user_id");
                    eb.MessageTime = (uint32_t)find_json_i64(e, "time");
                    eb.SourceGroupName = groupName.c_str();
                    eb.OperateQQName = "";
                    eb.TriggerQQName = senderName.c_str();
                    eb.MessageContent = content.c_str();
                    ret = safe_msg(fnEvent, &eb, "event callback");
                }
                emit_event("dispatch_result", ",\"ok\":true,\"result\":" + std::to_string(ret) + ",\"post_type\":\"" + json_escape(post) + "\",\"message_type\":\"" + json_escape(mt) + "\"");
                continue;
            }
            emit_event("error", ",\"error\":\"unsupported command\",\"action\":\"" + json_escape(action) + "\"");
        }
    }

    FreeLibrary(mod);
    return 0;
}

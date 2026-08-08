// main.cpp - LuckyBot Framework (JadeView Edition)
// WebView2 UI + OneBot 11 Backend + IPC Communication
// v1.0.1 - Formal release build

#include <cstdio>
#include <cstring>
#include <ctime>
#include <cctype>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <sstream>
#include <cmath>
#include <atomic>
#include <map>
#include <set>
#include <memory>
#include <algorithm>
#include <fstream>
#include <cstdarg>
#include <exception>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <commdlg.h>

// JadeView
#ifndef JADEVIEW_CALL
#define JADEVIEW_CALL __stdcall
#endif
#include "jade_dyn.h"
#include "embedded_ui.h"

// JSON
#include "../third_party/json.hpp"
#include "sdk/yuex_plugin_sdk.h"
#include "sdk/xlz_host_impl.h"
using json = nlohmann::json;
// ============================================================
static const char* kYuexBotVersion = "1.0.1";

static std::string strip_quotes(const std::string& s) {
    if (s.size() >= 2 && s[0] == '"') return s.substr(1, s.size() - 2);
    return s;
}

static int json_get_int(const json& j, const std::string& key, int default_val) {
    if (!j.contains(key)) return default_val;
    auto& v = j[key];
    if (v.is_number()) return v.get<int>();
    if (v.is_string()) { try { return std::stoi(v.get<std::string>()); } catch (...) { return default_val; } }
    return default_val;
}

static std::string normalize_ws_path(std::string path) {
    path.erase(std::remove_if(path.begin(), path.end(), [](unsigned char c) {
        return c == '\r' || c == '\n' || c == '\t' || c == ' ';
    }), path.end());
    if (path.empty()) return "/onebot/v11/ws";
    if (path[0] != '/') path.insert(path.begin(), '/');
    return path;
}

static std::string json_get_ws_path(const json& j, const std::string& fallback = "/onebot/v11/ws") {
    if (j.contains("path") && j["path"].is_string()) return normalize_ws_path(j["path"].get<std::string>());
    if (j.contains("wsPath") && j["wsPath"].is_string()) return normalize_ws_path(j["wsPath"].get<std::string>());
    return normalize_ws_path(fallback);
}

std::string json_value_string(const json& j, const std::string& key, const std::string& fallback) {
    if (!j.contains(key)) return fallback;
    const auto& v = j[key];
    if (v.is_string()) return v.get<std::string>();
    if (v.is_number_integer() || v.is_number_unsigned()) return std::to_string(v.get<int64_t>());
    if (v.is_number_float()) return std::to_string(v.get<double>());
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    return fallback;
}

int64_t json_value_i64(const json& j, const std::string& key, int64_t fallback) {
    if (!j.contains(key)) return fallback;
    const auto& v = j[key];
    if (v.is_number_integer() || v.is_number_unsigned()) return v.get<int64_t>();
    if (v.is_string()) { try { return std::stoll(v.get<std::string>()); } catch (...) { return fallback; } }
    return fallback;
}

static bool is_valid_utf8(const std::string& s) {
    int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     s.data(), (int)s.size(), NULL, 0);
    return needed > 0 || s.empty();
}

static std::string wide_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int needed = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), NULL, 0, NULL, NULL);
    if (needed <= 0) return "";
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(), &out[0], needed, NULL, NULL);
    return out;
}

static std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (needed <= 0) return L"";
    std::wstring out(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], needed);
    return out;
}

static std::string win_error_message_utf8(DWORD err) {
    if (err == 0) return "";
    wchar_t* buffer = nullptr;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&buffer, 0, NULL);
    std::wstring ws;
    if (len && buffer) ws.assign(buffer, len);
    if (buffer) LocalFree(buffer);
    while (!ws.empty() && (ws.back() == L'\r' || ws.back() == L'\n' || ws.back() == L' ' || ws.back() == L'\t')) ws.pop_back();
    return wide_to_utf8(ws);
}

static std::string plugin_load_error_reason(DWORD err) {
    std::string reason;
    if (err == ERROR_BAD_EXE_FORMAT) {
        reason = "架构不匹配或 DLL 格式无效，常见原因是 x64 主程序载入 x86/32 位插件";
    } else if (err == ERROR_MOD_NOT_FOUND) {
        reason = "插件依赖的 DLL 缺失，或插件文件路径不可访问";
    } else if (err == ERROR_PROC_NOT_FOUND) {
        reason = "插件依赖的入口函数缺失";
    } else if (err == ERROR_DLL_INIT_FAILED) {
        reason = "插件初始化失败，可能是依赖、运行库或 SDK 不兼容";
    } else if (err == ERROR_ACCESS_DENIED) {
        reason = "没有权限访问插件文件，或文件正在被占用";
    } else {
        reason = "未知加载错误";
    }
    std::string sys = win_error_message_utf8(err);
    if (!sys.empty()) reason += "；系统返回: " + sys;
    return reason + "；错误码 " + std::to_string((unsigned long)err);
}

static std::string ansi_to_utf8(const std::string& s) {
    if (s.empty()) return "";
    int needed = MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), NULL, 0);
    if (needed <= 0) return s;
    std::wstring ws(needed, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s.data(), (int)s.size(), &ws[0], needed);
    std::string out = wide_to_utf8(ws);
    return out.empty() ? s : out;
}

static bool looks_like_utf8_mojibake(const std::string& s) {
    static const char* markers[] = {
        "鎻", "鍔", "鍥", "娴", "璇", "涓", "浠", "鏂", "闂", "婵", "缂", "閿", "鈧", "瀹",
        "鏈", "堟", "邯", "缇", "ょ", "", "鍑", "礇", "鏄", "熼", "噹", "瀵", "昏"
    };
    for (auto marker : markers) {
        if (s.find(marker) != std::string::npos) return true;
    }
    return false;
}

static std::string repair_utf8_mojibake(const std::string& s) {
    if (!looks_like_utf8_mojibake(s)) return s;
    std::wstring ws = utf8_to_wide(s);
    if (ws.empty()) return s;
    int needed = WideCharToMultiByte(936, 0, ws.data(), (int)ws.size(), NULL, 0, NULL, NULL);
    if (needed <= 0) return s;
    std::string bytes(needed, '\0');
    WideCharToMultiByte(936, 0, ws.data(), (int)ws.size(), &bytes[0], needed, NULL, NULL);
    if (!is_valid_utf8(bytes)) return s;
    return bytes;
}

std::string normalize_external_text(const std::string& s) {
    if (s.empty()) return "";
    if (is_valid_utf8(s)) return repair_utf8_mojibake(s);
    return repair_utf8_mojibake(ansi_to_utf8(s));
}


// Custom runtime paths (release package layout)
// main/
//   bin/    YuexBot.exe + JadeView_x64.dll
//   plugin/ native plugin DLLs
//   UI/     generated index.html from embedded UI
//   corn/   account data
//   data/   framework settings and runtime data
// ============================================================
static std::string g_rootDir = ".";
static std::string g_packageDir = ".";
static std::string g_binDir = ".";
static std::string g_configDir = ".";
static std::string g_cornDir = ".";
static std::string g_pluginDir = "plugin";
static std::string g_uiDir = "UI";

static std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty() || a == ".") return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '\\' || last == '/') return a + b;
    return a + "\\" + b;
}

static std::string path_parent(const std::string& path) {
    size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

static std::string path_basename(std::string path) {
    size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) path = path.substr(slash + 1);
    return path;
}

static std::string lower_ascii(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

static bool ensure_directory_tree(const std::string& path) {
    if (path.empty()) return false;
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '/', '\\');
    size_t start = 0;
    if (normalized.size() >= 3 && normalized[1] == ':' && normalized[2] == '\\') start = 3;
    while (true) {
        size_t pos = normalized.find('\\', start);
        std::string part = normalized.substr(0, pos);
        if (!part.empty()) {
            DWORD attrs = GetFileAttributesA(part.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES) {
                if (!CreateDirectoryA(part.c_str(), NULL)) {
                    DWORD err = GetLastError();
                    if (err != ERROR_ALREADY_EXISTS) return false;
                }
            } else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
                return false;
            }
        }
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    return true;
}

static bool file_exists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static bool dir_exists(const std::string& path) {
    DWORD attrs = GetFileAttributesA(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

static void copy_file_if_missing(const std::string& from, const std::string& to) {
    if (!file_exists(from) || file_exists(to)) return;
    ensure_directory_tree(path_parent(to));
    CopyFileA(from.c_str(), to.c_str(), TRUE);
}

static void copy_directory_files_if_missing(const std::string& fromDir, const std::string& toDir) {
    if (!dir_exists(fromDir)) return;
    ensure_directory_tree(toDir);
    std::string pattern = path_join(fromDir, "*");
    WIN32_FIND_DATAW fd;
    std::wstring wpattern = utf8_to_wide(pattern);
    HANDLE h = FindFirstFileW(wpattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = wide_to_utf8(fd.cFileName);
        std::string src = path_join(fromDir, name);
        std::string dst = path_join(toDir, name);
        if (!file_exists(dst)) {
            std::wstring wsrc = utf8_to_wide(src);
            std::wstring wdst = utf8_to_wide(dst);
            if (!wsrc.empty() && !wdst.empty()) CopyFileW(wsrc.c_str(), wdst.c_str(), TRUE);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static void migrate_legacy_layout_files() {
    copy_file_if_missing(path_join(g_rootDir, "config.json"), path_join(g_configDir, "config.json"));
    copy_file_if_missing(path_join(g_rootDir, "settings.json"), path_join(g_configDir, "settings.json"));
    copy_file_if_missing(path_join(g_rootDir, "accounts.json"), path_join(g_cornDir, "accounts.json"));
    copy_directory_files_if_missing(path_join(g_rootDir, "plugins"), g_pluginDir);
    if (g_packageDir != g_rootDir) {
        copy_file_if_missing(path_join(g_packageDir, "config.json"), path_join(g_configDir, "config.json"));
        copy_file_if_missing(path_join(g_packageDir, "settings.json"), path_join(g_configDir, "settings.json"));
        copy_file_if_missing(path_join(g_packageDir, "accounts.json"), path_join(g_cornDir, "accounts.json"));
        copy_directory_files_if_missing(path_join(g_packageDir, "plugins"), g_pluginDir);
    }
}

static void initialize_runtime_paths() {
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exe(exePath);
    std::string exeDir = path_parent(exe);
    if (exeDir.empty()) exeDir = ".";

    g_packageDir = exeDir;
    std::string dirName = lower_ascii(path_basename(exeDir));
    if (dirName == "bin") {
        g_rootDir = path_parent(exeDir);
        g_packageDir = path_parent(g_rootDir);
        g_binDir = exeDir;
    } else if (dirName == "main") {
        g_rootDir = exeDir;
        g_packageDir = path_parent(exeDir);
        g_binDir = path_join(g_rootDir, "bin");
    } else if (dir_exists(path_join(exeDir, "main"))) {
        g_rootDir = path_join(exeDir, "main");
        g_packageDir = exeDir;
        g_binDir = path_join(g_rootDir, "bin");
    } else {
        g_rootDir = exeDir;
        g_binDir = path_join(g_rootDir, "bin");
    }
    if (g_rootDir.empty()) g_rootDir = exeDir;

    g_configDir = path_join(g_rootDir, "data");
    g_cornDir = path_join(g_rootDir, "corn");
    g_pluginDir = path_join(g_rootDir, "plugin");
    g_uiDir = path_join(g_rootDir, "UI");

    ensure_directory_tree(g_rootDir);
    ensure_directory_tree(g_configDir);
    ensure_directory_tree(g_cornDir);
    ensure_directory_tree(g_pluginDir);
    ensure_directory_tree(g_uiDir);
    migrate_legacy_layout_files();
    SetCurrentDirectoryA(g_rootDir.c_str());
}

static bool write_embedded_frontend(std::string& outDir, std::string& outIndexPath) {
    outDir = g_uiDir.empty() ? "UI" : g_uiDir;
    outIndexPath = path_join(outDir, "index.html");
    if (!ensure_directory_tree(outDir)) return false;

    HANDLE f = CreateFileA(outIndexPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, kEmbeddedIndexHtml, (DWORD)kEmbeddedIndexHtmlSize, &written, NULL);
    CloseHandle(f);
    return ok && written == (DWORD)kEmbeddedIndexHtmlSize;
}

static void startup_trace(const char* fmt, ...) {
    if (!fmt) return;
    ensure_directory_tree(g_configDir);
    std::string logPath = path_join(g_configDir, "startup_trace.log");
    std::string oldLogPath = path_join(g_configDir, "startup_trace.old.log");
    std::wstring wLogPath = utf8_to_wide(logPath);
    std::wstring wOldLogPath = utf8_to_wide(oldLogPath);
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!wLogPath.empty() && GetFileAttributesExW(wLogPath.c_str(), GetFileExInfoStandard, &fad)) {
        ULONGLONG size = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        if (size > 1024ull * 1024ull) {
            if (!wOldLogPath.empty()) DeleteFileW(wOldLogPath.c_str());
            if (!wOldLogPath.empty()) MoveFileExW(wLogPath.c_str(), wOldLogPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
    HANDLE f = !wLogPath.empty() ? CreateFileW(wLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (f == INVALID_HANDLE_VALUE) return;
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    char line[768];
    int n = snprintf(line, sizeof(line), "%04d-%02d-%02d %02d:%02d:%02d %s\r\n",
                     st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg);
    if (n > 0) {
        DWORD written = 0;
        int writeLen = n < (int)sizeof(line) ? n : (int)sizeof(line) - 1;
        WriteFile(f, line, (DWORD)writeLen, &written, NULL);
    }
    CloseHandle(f);
}

static LONG WINAPI unhandled_exception_filter(EXCEPTION_POINTERS* ep) {
    DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
    void* addr = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
    startup_trace("unhandled exception code=0x%08X addr=%p", code, addr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void yuex_terminate_handler() {
    startup_trace("std::terminate called");
    ExitProcess(1);
}

static bool is_running_as_admin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0,
                                 &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

static bool relaunch_as_admin_if_needed() {
    if (is_running_as_admin()) return false;
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string workDir = exePath;
    size_t slash = workDir.find_last_of("\\/");
    if (slash != std::string::npos) workDir.resize(slash);
    startup_trace("not elevated, relaunching as administrator");
    HINSTANCE r = ShellExecuteA(NULL, "runas", exePath, NULL,
                                workDir.empty() ? NULL : workDir.c_str(), SW_SHOWNORMAL);
    if ((INT_PTR)r > 32) return true;
    startup_trace("admin relaunch failed code=%zd", (size_t)r);
    return false;
}
static void save_config_file(const std::string& filename, const json& data) {
    std::string path = path_join(g_configDir, filename);
    ensure_directory_tree(path_parent(path));
    std::string s = data.dump(2);
    std::wstring wpath = utf8_to_wide(path);
    HANDLE f = !wpath.empty() ? CreateFileW(wpath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, s.data(), (DWORD)s.size(), &written, NULL);
    CloseHandle(f);
}

static json load_config_file(const std::string& filename) {
    std::string path = path_join(g_configDir, filename);
    std::wstring wpath = utf8_to_wide(path);
    HANDLE f = !wpath.empty() ? CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) : INVALID_HANDLE_VALUE;
    if (f == INVALID_HANDLE_VALUE) return json();
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(f, &size) || size.QuadPart <= 0 || size.QuadPart > 16 * 1024 * 1024) {
        CloseHandle(f);
        return json();
    }
    std::string buf((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    ReadFile(f, &buf[0], (DWORD)buf.size(), &read, NULL);
    CloseHandle(f);
    if (read < buf.size()) buf.resize(read);
    try {
        return json::parse(buf);
    } catch (const std::exception& e) {
        std::string backup = path + ".bad." + std::to_string((long long)time(nullptr));
        std::wstring wbackup = utf8_to_wide(backup);
        if (!wpath.empty() && !wbackup.empty()) CopyFileW(wpath.c_str(), wbackup.c_str(), FALSE);
        startup_trace("config parse failed file=%s error=%s backup=%s", filename.c_str(), e.what(), backup.c_str());
        return json::object();
    } catch (...) {
        std::string backup = path + ".bad." + std::to_string((long long)time(nullptr));
        std::wstring wbackup = utf8_to_wide(backup);
        if (!wpath.empty() && !wbackup.empty()) CopyFileW(wpath.c_str(), wbackup.c_str(), FALSE);
        startup_trace("config parse failed file=%s backup=%s", filename.c_str(), backup.c_str());
        return json::object();
    }
}


// OneBot 11 HTTP client
#include "../src/network.h"
#include "../src/onebot.h"
#include "../src/ws.h"

// ============================================================
// Global State
// ============================================================
static uint32_t g_windowId = 0;
static OneBotClient g_onebot;
static std::atomic<bool> g_connected{false};
static std::atomic<bool> g_autoReconnect{false};
static std::atomic<bool> g_shuttingDown{false};
static std::atomic<bool> g_exitRequested{false};
static std::mutex g_mutex;

struct LogEntry {
    std::string time, type, sender, content;
    std::string account_id, account_name;
    std::string post_type, message_type, notice_type, request_type, sub_type;
    std::string sender_name, group_name, self_id, message_id;
    std::string flag, comment;
    std::string raw_event;
    int64_t sender_id = 0, group_id = 0;
    int64_t target_id = 0, operator_id = 0;
};

static std::deque<LogEntry> g_logs;
static constexpr size_t kMaxLogEntries = 300;
static constexpr size_t kMaxWsPreviewChars = 160;
static constexpr size_t kMaxWsResponseEntries = 64;
static std::atomic<int64_t> g_lastWorkingSetTrim{0};
static std::vector<json> g_friends;
static std::vector<json> g_groups;
static std::map<std::string, std::vector<json>> g_accountFriends;
static std::map<std::string, std::vector<json>> g_accountGroups;
static int64_t g_startTime = 0;
static std::atomic<int> g_totalEvents{0};
static std::atomic<int> g_totalMessages{0};
static std::atomic<int> g_latencyMs{0};

static void trim_working_set_soft();

// Connection config (for reconnect)
static std::string g_host = "127.0.0.1";
static int g_port = 3001;
static std::string g_token;
static std::string g_wsPath = "/onebot/v11/ws";
static std::string g_connMode = "http-post";  // reverse-ws, forward-ws, http-post
static WebSocketClient g_ws;
static std::string g_activeAccountId;
static std::mutex g_reverseWsMutex;
static std::atomic<bool> g_reverseWsListening{false};
static SOCKET g_reverseWsSocket = INVALID_SOCKET;
static int g_reverseWsPort = 0;
static std::string g_reverseWsHost;
static std::string g_reverseWsToken;

struct PluginRuntime {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string path;
    bool enabled = false;
    HMODULE module = nullptr;
    YuexPluginInitFn init = nullptr;
    YuexPluginShutdownFn shutdown = nullptr;
    YuexPluginOnEventFn onEvent = nullptr;
    int (YUEX_PLUGIN_CALL *openSettings)() = nullptr;
    std::vector<std::string> permissions;
    uint32_t eventMask = 0;
    uint64_t eventCount = 0;
    uint64_t eventErrorCount = 0;
    uint64_t slowEventCount = 0;
    uint64_t totalEventMs = 0;
    uint64_t maxEventMs = 0;
    int64_t lastEventAt = 0;
    int64_t lastErrorAt = 0;
    int lastEventResult = 0;
    std::string lastError;
};

struct ReservedPluginRuntime {
    std::string id;
    std::string name;
    std::string description;
    std::string path;
    std::string type;
    std::string reason;
    bool manageable = true;
};

struct XlzBridgeRuntime {
      std::string id, name, version, author, description, path, pluginKey, appInfoRaw, sdkVersion, lastError;
      std::string pluginKind = "xiaolizi-x86";
      std::string lastHostEvent, lastEventType, lastMessageType, lastNoticeType, lastApiAction;
      bool enabled = false;
      bool enableOk = false;
      bool hasSettings = false;
      bool hasGroupCallback = false;
      bool hasPrivateCallback = false;
      bool hasEventCallback = false;
      bool hasEnableCallback = false;
      DWORD processId = 0;
    HANDLE process = NULL;
    HANDLE stdinWrite = NULL;
    std::thread stdoutThread;
    std::atomic<bool> readerRunning{false};
    std::atomic<uint64_t> dispatchCount{0};
    std::atomic<uint64_t> dispatchOkCount{0};
    std::atomic<uint64_t> apiCount{0};
    std::atomic<uint64_t> errorCount{0};
    std::atomic<int64_t> lastEventAt{0};
    std::atomic<int64_t> lastApiAt{0};
    std::atomic<int64_t> lastHostAt{0};
};

HWND g_mainHwnd = NULL;
static std::vector<PluginRuntime> g_plugins;
static std::vector<ReservedPluginRuntime> g_reservedPlugins;
static std::vector<std::shared_ptr<XlzBridgeRuntime>> g_xlzBridgePlugins;
static std::mutex g_pluginMutex;
struct PluginEventJob {
    int type = 0;
    std::string body;
};
static std::mutex g_pluginEventMutex;
static std::condition_variable g_pluginEventCv;
static std::deque<PluginEventJob> g_pluginEventQueue;
static std::thread g_pluginEventThread;
static std::atomic<bool> g_pluginEventWorkerRunning{false};
static std::mutex g_pluginDispatchMutex;
static std::atomic<uint64_t> g_pluginEventsQueued{0};
static std::atomic<uint64_t> g_pluginEventsDropped{0};
static const size_t kPluginEventQueueLimit = 4096;
static YuexBotApi g_pluginApi = {};
static std::mutex g_pluginApiMutex;
static std::string g_pluginApiLastResult;
static std::string g_pluginApiLastError;
static std::string g_pluginApiScratch;
static std::mutex g_pluginOpMutex;
static std::string g_lastPluginOpError;
static thread_local std::string g_pluginCallContextId;
static bool g_xiaoliziCompatEnabled = false;


// ============================================================
// Multi-Account Management
// ============================================================
struct AccountConfig {
    std::string id;
    std::string name;
    std::string qq;
    std::string mode;    // reverse-ws, forward-ws, http-post
    std::string host;
    int port = 3001;
    std::string path;
    std::string token;
    std::string apiHost;
    int apiPort = 0;
    std::string apiToken;
    std::string status;  // offline, connecting, online
    bool autoConnect = false;
};

static std::vector<AccountConfig> g_accounts;

struct AccountRuntime {
    AccountConfig config;
    std::atomic<bool> connected{false};
    std::atomic<bool> connecting{false};
    std::string loginQq;
    std::string nickname;
    std::vector<json> friends;
    std::vector<json> groups;
    int latencyMs = 0;
    int64_t lastEventTime = 0;
    std::shared_ptr<WebSocketClient> ws;
    std::shared_ptr<OneBotClient> http;
    std::mutex apiMutex;
    std::mutex wsResponseMutex;
    std::map<std::string, json> wsResponses;
    std::atomic<int> wsEcho{0};
    std::atomic<bool> wsApiAvailable{false};
    std::atomic<int> wsMessages{0};
    std::atomic<int> wsEvents{0};
    std::atomic<int> wsUnknownMessages{0};
    std::atomic<int> reconnectAttempts{0};
    std::atomic<int64_t> nextReconnectAt{0};
    std::atomic<bool> manualStopped{false};
    std::string lastWsPreview;
    std::string lastError;
    int64_t lastConnectAt = 0;
    std::string mode;
    std::string host;
    int port = 0;
    std::string path;
    std::string token;
    std::string apiHost;
    int apiPort = 0;
    std::string apiToken;
};

static std::mutex g_accountRuntimeMutex;
static std::map<std::string, std::shared_ptr<AccountRuntime>> g_accountRuntimes;

static std::shared_ptr<AccountRuntime> ensure_account_runtime_locked(const AccountConfig& cfg) {
    auto id = cfg.id;
    if (id.empty()) return nullptr;
    auto it = g_accountRuntimes.find(id);
    if (it == g_accountRuntimes.end()) {
        auto rt = std::make_shared<AccountRuntime>();
        rt->config = cfg;
        rt->loginQq = cfg.qq;
        rt->nickname = cfg.name;
        rt->connected = (cfg.status == "online");
        rt->connecting = (cfg.status == "connecting");
        rt->mode = cfg.mode;
        rt->host = cfg.host;
        rt->port = cfg.port;
        rt->path = cfg.path.empty() ? "/onebot/v11/ws" : normalize_ws_path(cfg.path);
        rt->token = cfg.token;
        rt->apiHost = cfg.apiHost.empty() ? cfg.host : cfg.apiHost;
        rt->apiPort = cfg.apiPort;
        rt->apiToken = cfg.apiToken.empty() ? cfg.token : cfg.apiToken;
        rt->ws = std::make_shared<WebSocketClient>();
        rt->http = std::make_shared<OneBotClient>();
        rt->http->setConfig(rt->apiHost.empty() ? rt->host : rt->apiHost,
                            rt->apiPort > 0 ? rt->apiPort : rt->port,
                            rt->apiToken);
        g_accountRuntimes[id] = rt;
        return rt;
    }
    it->second->config = cfg;
    if (!cfg.qq.empty()) it->second->loginQq = cfg.qq;
    if (!cfg.name.empty()) it->second->nickname = cfg.name;
    it->second->connected = (cfg.status == "online");
    it->second->connecting = (cfg.status == "connecting");
    it->second->mode = cfg.mode;
    it->second->host = cfg.host;
    it->second->port = cfg.port;
    it->second->path = cfg.path.empty() ? "/onebot/v11/ws" : normalize_ws_path(cfg.path);
    it->second->token = cfg.token;
    it->second->apiHost = cfg.apiHost.empty() ? cfg.host : cfg.apiHost;
    it->second->apiPort = cfg.apiPort;
    it->second->apiToken = cfg.apiToken.empty() ? cfg.token : cfg.apiToken;
    if (!it->second->ws) it->second->ws = std::make_shared<WebSocketClient>();
    if (!it->second->http) it->second->http = std::make_shared<OneBotClient>();
    it->second->http->setConfig(it->second->apiHost.empty() ? it->second->host : it->second->apiHost,
                                it->second->apiPort > 0 ? it->second->apiPort : it->second->port,
                                it->second->apiToken);
    return it->second;
}

static void sync_account_runtimes_from_configs() {
    std::lock_guard<std::mutex> lock(g_accountRuntimeMutex);
    std::map<std::string, bool> alive;
    for (auto& a : g_accounts) {
        if (a.id.empty()) continue;
        alive[a.id] = true;
        ensure_account_runtime_locked(a);
    }
    for (auto it = g_accountRuntimes.begin(); it != g_accountRuntimes.end();) {
        if (!alive[it->first]) it = g_accountRuntimes.erase(it);
        else ++it;
    }
}

static std::shared_ptr<AccountRuntime> get_account_runtime(const std::string& id) {
    std::lock_guard<std::mutex> lock(g_accountRuntimeMutex);
    auto it = g_accountRuntimes.find(id);
    return it == g_accountRuntimes.end() ? nullptr : it->second;
}

static std::string find_account_id_by_self_id(const std::string& selfId) {
    if (selfId.empty()) return "";
    std::lock_guard<std::mutex> lock(g_accountRuntimeMutex);
    for (const auto& a : g_accounts) {
        auto it = g_accountRuntimes.find(a.id);
        std::string login = (it != g_accountRuntimes.end()) ? it->second->loginQq : "";
        if (a.qq == selfId || login == selfId) return a.id;
    }
    return "";
}

static std::string account_display_name(const std::string& accountId) {
    if (accountId.empty()) return "";
    std::lock_guard<std::mutex> lock(g_accountRuntimeMutex);
    for (const auto& a : g_accounts) {
        if (a.id == accountId) {
            auto it = g_accountRuntimes.find(accountId);
            std::string name = (it != g_accountRuntimes.end()) ? it->second->nickname : "";
            if (name.empty()) name = a.name;
            if (name.empty()) name = (it != g_accountRuntimes.end()) ? it->second->loginQq : "";
            if (name.empty()) name = a.qq;
            return name.empty() ? accountId : name;
        }
    }
    return accountId;
}

static void update_account_runtime_status(const std::string& id, const std::string& status) {
    if (id.empty()) return;
    std::lock_guard<std::mutex> lock(g_accountRuntimeMutex);
    for (auto& a : g_accounts) {
        if (a.id == id) {
            a.status = status;
            auto rt = ensure_account_runtime_locked(a);
            if (rt) {
                rt->connected = (status == "online");
                rt->connecting = (status == "connecting");
            }
            return;
        }
    }
}

static void update_account_runtime_login(const std::string& id, const std::string& qq, const std::string& name) {
    if (id.empty()) return;
    std::lock_guard<std::mutex> lock(g_accountRuntimeMutex);
    for (auto& a : g_accounts) {
        if (a.id == id) {
            if (!qq.empty()) a.qq = qq;
            if (!name.empty()) a.name = name;
            auto rt = ensure_account_runtime_locked(a);
            if (rt) {
                if (!qq.empty()) rt->loginQq = qq;
                if (!name.empty()) rt->nickname = name;
            }
            return;
        }
    }
}

static void save_accounts_to_yaml() {
    json arr = json::array();
    for (auto& a : g_accounts) {
        json j;
        j["id"] = a.id; j["name"] = a.name; j["qq"] = a.qq;
        j["mode"] = a.mode; j["host"] = a.host; j["port"] = a.port;
        j["path"] = a.path.empty() ? "/onebot/v11/ws" : a.path;
        j["token"] = a.token;
        j["api_host"] = a.apiHost;
        j["api_port"] = a.apiPort;
        j["api_token"] = a.apiToken;
        j["auto_connect"] = a.autoConnect;
        arr.push_back(j);
    }
    ensure_directory_tree(g_cornDir);
    std::string path = path_join(g_cornDir, "accounts.json");
    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        std::string s = arr.dump(2);
        fwrite(s.c_str(), 1, s.size(), f);
        fclose(f);
    }
    if (yaml_set) yaml_set("accounts.yaml", "list", arr.dump().c_str());
}

static void load_accounts_from_yaml() {
    // Try custom file first, then yaml_get
    json arr;
    {
        std::string path = path_join(g_cornDir, "accounts.json");
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::string buf(sz, '\0');
            fread(&buf[0], 1, sz, f);
            fclose(f);
            try { arr = json::parse(buf); } catch (...) { arr = json(); }
        }
    }
    if (!arr.is_array()) arr = load_config_file("accounts.json");
    if (!arr.is_array() && yaml_get) {
        char buf[8192];
        if (yaml_get("accounts.yaml", "list", buf, sizeof(buf)) > 0) {
            try { arr = json::parse(buf); } catch (...) {}
        }
    }
    if (!arr.is_array()) return;
    try {
        g_accounts.clear();
        for (auto& j : arr) {
            AccountConfig a;
            a.id = normalize_external_text(j.value("id", ""));
            a.name = normalize_external_text(j.value("name", ""));
            a.qq = normalize_external_text(j.value("qq", ""));
            a.mode = normalize_external_text(j.value("mode", "reverse-ws"));
            a.host = normalize_external_text(j.value("host", "127.0.0.1"));
            a.port = j.value("port", 3001);
            a.path = normalize_external_text(json_get_ws_path(j));
            a.token = normalize_external_text(j.value("token", ""));
            a.apiHost = normalize_external_text(j.value("api_host", j.value("apiHost", "")));
            a.apiPort = j.value("api_port", j.value("apiPort", 0));
            a.apiToken = normalize_external_text(j.value("api_token", j.value("apiToken", "")));
            a.autoConnect = j.value("auto_connect", j.value("autoConnect", false));
            a.status = "offline";
            if (!a.id.empty()) g_accounts.push_back(a);
        }
        sync_account_runtimes_from_configs();
    } catch (...) {}
}

static json accounts_to_json() {
    sync_account_runtimes_from_configs();
    json arr = json::array();
    for (auto& a : g_accounts) {
        json j;
        j["id"] = a.id; j["name"] = a.name; j["qq"] = a.qq;
        j["mode"] = a.mode; j["host"] = a.host; j["port"] = a.port;
        j["path"] = a.path.empty() ? "/onebot/v11/ws" : a.path;
        j["token"] = a.token; j["status"] = a.status;
        j["api_host"] = a.apiHost;
        j["api_port"] = a.apiPort;
        j["api_token"] = a.apiToken;
        j["auto_connect"] = a.autoConnect;
        j["connected"] = (a.status == "online");
        j["connecting"] = (a.status == "connecting");
        auto rt = get_account_runtime(a.id);
        if (rt) {
            j["runtime_connected"] = rt->connected.load();
            j["runtime_connecting"] = rt->connecting.load();
            j["login_qq"] = rt->loginQq;
            j["nickname"] = rt->nickname;
            j["friend_count"] = (int)rt->friends.size();
            j["group_count"] = (int)rt->groups.size();
            j["latency"] = rt->latencyMs;
            j["ws_messages"] = rt->wsMessages.load();
            j["ws_events"] = rt->wsEvents.load();
            j["ws_api_available"] = rt->wsApiAvailable.load();
            j["last_ws_preview"] = rt->lastWsPreview;
            j["effective_path"] = rt->path;
            j["reconnect_attempts"] = rt->reconnectAttempts.load();
            j["next_reconnect_at"] = rt->nextReconnectAt.load();
            j["manual_stopped"] = rt->manualStopped.load();
            j["last_error"] = rt->lastError;
            j["last_connect_at"] = rt->lastConnectAt;
            j["event_channel"] = (rt->ws && rt->ws->isConnected()) ? "online" : "offline";
            j["api_channel"] = rt->wsApiAvailable.load() || (rt->http && rt->http->isConnected()) ? "online" : "offline";
        }
        arr.push_back(j);
    }
    return arr;
}

static int find_account(const std::string& id) {
    for (int i = 0; i < (int)g_accounts.size(); i++) {
        if (g_accounts[i].id == id) return i;
    }
    return -1;
}

static std::string resolve_account_id_for_login(const std::string& qq, const std::string& host, int port) {
    for (auto& a : g_accounts) {
        if ((!qq.empty() && a.qq == qq) || (a.host == host && a.port == port)) {
            if (!qq.empty()) a.qq = qq;
            a.status = "online";
            return a.id;
        }
    }
    if (!qq.empty()) return "acc_" + qq;
    return g_activeAccountId;
}

static json compact_friend_item(const json& f) {
    json item;
    item["user_id"] = json_value_i64(f, "user_id");
    item["nickname"] = normalize_external_text(json_value_string(f, "nickname"));
    item["remark"] = normalize_external_text(json_value_string(f, "remark"));
    return item;
}

static json compact_group_item(const json& g) {
    json item;
    item["group_id"] = json_value_i64(g, "group_id");
    item["group_name"] = normalize_external_text(json_value_string(g, "group_name"));
    item["member_count"] = json_value_i64(g, "member_count");
    item["max_member_count"] = json_value_i64(g, "max_member_count");
    return item;
}

json call_onebot_api_bridge_for_account(const std::string& accountRef, const std::string& action, const json& params);

static void cache_account_lists(const std::string& accountId, const json& friends, const json& groups) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_friends.clear();
    if (friends.contains("data") && friends["data"].is_array()) {
        g_friends.reserve(friends["data"].size());
        for (const auto& f : friends["data"]) g_friends.push_back(compact_friend_item(f));
    }
    g_friends.shrink_to_fit();
    g_groups.clear();
    if (groups.contains("data") && groups["data"].is_array()) {
        g_groups.reserve(groups["data"].size());
        for (const auto& gg : groups["data"]) g_groups.push_back(compact_group_item(gg));
    }
    g_groups.shrink_to_fit();
    if (!accountId.empty()) {
        g_accountFriends[accountId] = g_friends;
        g_accountGroups[accountId] = g_groups;
        auto rt = get_account_runtime(accountId);
        if (rt) {
            rt->friends = g_friends;
            rt->groups = g_groups;
        }
    }
    trim_working_set_soft();
}

static json compact_account_list_payload(const std::string& kind, const json& resp) {
    json arr = json::array();
    const json* src = nullptr;
    if (resp.is_array()) src = &resp;
    else if (resp.is_object() && resp.contains("data") && resp["data"].is_array()) src = &resp["data"];
    if (!src) return arr;
    for (const auto& item : *src) {
        arr.push_back(kind == "friends" ? compact_friend_item(item) : compact_group_item(item));
    }
    return arr;
}

static void cache_account_single_list(const std::string& accountId, const std::string& kind, const json& arr) {
    if (!arr.is_array()) return;
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<json> list;
    list.reserve(arr.size());
    for (const auto& item : arr) list.push_back(item);
    list.shrink_to_fit();
    if (kind == "friends") {
        g_friends = list;
        if (!accountId.empty()) g_accountFriends[accountId] = list;
        auto rt = get_account_runtime(accountId);
        if (rt) rt->friends = list;
    } else {
        g_groups = list;
        if (!accountId.empty()) g_accountGroups[accountId] = list;
        auto rt = get_account_runtime(accountId);
        if (rt) rt->groups = list;
    }
    trim_working_set_soft();
}

static json get_account_list_payload(const std::string& kind, const char* data) {
    std::string accountId;
    try {
        if (data && std::strlen(data) > 0) {
            auto j = json::parse(data);
            accountId = j.value("account_id", j.value("id", ""));
        }
    } catch (...) {}
    if (accountId.empty()) accountId = g_activeAccountId;

    json r;
    r["account_id"] = accountId;
    std::string liveError;
    if (!accountId.empty()) {
        const std::string action = kind == "friends" ? "get_friend_list" : "get_group_list";
        json live = call_onebot_api_bridge_for_account(accountId, action, json::object());
        json liveList = compact_account_list_payload(kind, live);
        if ((live.is_array()) || (live.is_object() && live.contains("data") && live["data"].is_array())) {
            cache_account_single_list(accountId, kind, liveList);
            r["data"] = liveList;
            return r;
        }
        if (live.is_object()) liveError = live.value("message", live.value("wording", live.value("msg", "")));
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    if (kind == "friends") {
        auto it = g_accountFriends.find(accountId);
        r["data"] = (it != g_accountFriends.end()) ? it->second : g_friends;
    } else {
        auto it = g_accountGroups.find(accountId);
        r["data"] = (it != g_accountGroups.end()) ? it->second : g_groups;
    }
    if (r["data"].empty() && !liveError.empty()) r["error"] = liveError;
    return r;
}
// Event callback HTTP server
static std::atomic<bool> g_eventServerRunning{false};
static int g_eventServerPort = 5701;
static std::mutex g_eventServerMutex;
static SOCKET g_eventServerSocket = INVALID_SOCKET;

static void close_event_server_socket() {
    std::lock_guard<std::mutex> lock(g_eventServerMutex);
    if (g_eventServerSocket != INVALID_SOCKET) {
        closesocket(g_eventServerSocket);
        g_eventServerSocket = INVALID_SOCKET;
    }
}

// ============================================================
// Utilities
// ============================================================
static std::string now_str() {
    time_t t = time(nullptr);
    struct tm ti;
    localtime_s(&ti, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
    return buf;
}

static std::string uptime_str() {
    int sec = (int)(time(nullptr) - g_startTime);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", sec/3600, (sec%3600)/60, sec%60);
    return buf;
}

static void push_to_frontend(const char* event, const std::string& data) {
    if (g_windowId > 0) {
        send_ipc_message(g_windowId, event, data.c_str());
    }
}

static void trim_working_set_soft() {
    int64_t now = (int64_t)time(nullptr);
    int64_t last = g_lastWorkingSetTrim.load();
    if (now - last < 30) return;
    if (!g_lastWorkingSetTrim.compare_exchange_strong(last, now)) return;
    EmptyWorkingSet(GetCurrentProcess());
}

static float process_working_set_mb() {
    PROCESS_MEMORY_COUNTERS pmc = {};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) return 0.0f;
    return (float)pmc.WorkingSetSize / (1024.0f * 1024.0f);
}

static void trim_ws_response_cache_locked(std::map<std::string, json>& responses) {
    while (responses.size() > kMaxWsResponseEntries) {
        responses.erase(responses.begin());
    }
}

static void trim_working_set_now() {
    g_lastWorkingSetTrim = (int64_t)time(nullptr);
    EmptyWorkingSet(GetCurrentProcess());
}

static std::string cached_group_name(int64_t groupId) {
    if (groupId <= 0) return "";
    std::lock_guard<std::mutex> lock(g_mutex);
    for (const auto& g : g_groups) {
        if (json_value_i64(g, "group_id") == groupId) {
            return json_value_string(g, "group_name");
        }
    }
    return "";
}

static void connect_account_runtime(const std::string& accountId);

static bool global_auto_connect_accounts_enabled() {
    auto settings = load_config_file("settings.json");
    return settings.value("autoConnectAccounts", false);
}

static bool account_should_auto_connect(const AccountConfig& a, bool globalAuto) {
    return globalAuto || a.autoConnect;
}

static void account_reconnect_watch_thread() {
    while (!g_shuttingDown) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        if (g_shuttingDown) break;
        bool globalAuto = global_auto_connect_accounts_enabled();
        std::vector<std::string> startIds;
        int64_t now = (int64_t)time(nullptr);
        {
            std::lock_guard<std::mutex> lock(g_accountRuntimeMutex);
            for (const auto& a : g_accounts) {
                if (a.id.empty() || !account_should_auto_connect(a, globalAuto)) continue;
                if (a.mode != "forward-ws" && a.mode != "http-post") continue;
                auto rt = ensure_account_runtime_locked(a);
                if (!rt || rt->manualStopped.load() || rt->connected.load() || rt->connecting.load()) continue;
                bool transportAlive = (rt->ws && rt->ws->isConnected()) || (rt->http && rt->http->isConnected());
                if (transportAlive) continue;
                int64_t nextAt = rt->nextReconnectAt.load();
                if (nextAt > now) continue;
                int attempt = rt->reconnectAttempts.load() + 1;
                rt->reconnectAttempts = attempt;
                int delay = 10 + (attempt > 6 ? 60 : attempt * 8);
                if (delay > 90) delay = 90;
                rt->nextReconnectAt = now + delay;
                startIds.push_back(a.id);
            }
        }
        for (const auto& id : startIds) {
            add_log("system", account_display_name(id), "自动重连账号，尝试次数 " + std::to_string(get_account_runtime(id) ? get_account_runtime(id)->reconnectAttempts.load() : 0), 0, 0, id, account_display_name(id));
            connect_account_runtime(id);
        }
    }
}

static std::string cached_group_name_for_account(const std::string& accountId, int64_t groupId) {
    if (groupId <= 0) return "";
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!accountId.empty()) {
        auto it = g_accountGroups.find(accountId);
        if (it != g_accountGroups.end()) {
            for (const auto& g : it->second) {
                if (json_value_i64(g, "group_id") == groupId) return json_value_string(g, "group_name");
            }
        }
    }
    for (const auto& g : g_groups) {
        if (json_value_i64(g, "group_id") == groupId) return json_value_string(g, "group_name");
    }
    return "";
}

static json log_entry_to_json(const LogEntry& entry) {
    json j;
    j["time"] = entry.time;
    j["type"] = entry.type;
    j["sender"] = entry.sender;
    j["content"] = entry.content;
    j["message"] = entry.content;  // alias for frontend search
    j["sender_id"] = entry.sender_id;
    j["group_id"] = entry.group_id;
    j["target_id"] = entry.target_id;
    j["operator_id"] = entry.operator_id;
    j["account_id"] = entry.account_id;
    j["account_name"] = entry.account_name;
    j["post_type"] = entry.post_type;
    j["message_type"] = entry.message_type;
    j["notice_type"] = entry.notice_type;
    j["request_type"] = entry.request_type;
    j["sub_type"] = entry.sub_type;
    j["sender_name"] = entry.sender_name;
    j["group_name"] = entry.group_name;
    j["self_id"] = entry.self_id;
    j["message_id"] = entry.message_id;
    j["flag"] = entry.flag;
    j["comment"] = entry.comment;
    j["raw_event"] = entry.raw_event;
    return j;
}

void add_log(const std::string& type, const std::string& sender,
                    const std::string& content, int64_t sender_id, int64_t group_id,
                    const std::string& account_id, const std::string& account_name,
                    const json& detail) {
    std::lock_guard<std::mutex> lock(g_mutex);
    LogEntry entry;
    entry.time = now_str();
    entry.type = normalize_external_text(type);
    entry.sender = normalize_external_text(sender);
    entry.content = normalize_external_text(content);
    entry.account_id = account_id;
    entry.account_name = normalize_external_text(account_name);
    entry.sender_id = sender_id;
    entry.group_id = group_id;
    if (detail.is_object()) {
        entry.post_type = normalize_external_text(json_value_string(detail, "post_type"));
        entry.message_type = normalize_external_text(json_value_string(detail, "message_type"));
        entry.notice_type = normalize_external_text(json_value_string(detail, "notice_type"));
        entry.request_type = normalize_external_text(json_value_string(detail, "request_type"));
        entry.sub_type = normalize_external_text(json_value_string(detail, "sub_type"));
        entry.sender_name = normalize_external_text(json_value_string(detail, "sender_name"));
        entry.group_name = normalize_external_text(json_value_string(detail, "group_name"));
        entry.self_id = normalize_external_text(json_value_string(detail, "self_id"));
        entry.message_id = normalize_external_text(json_value_string(detail, "message_id"));
        entry.flag = normalize_external_text(json_value_string(detail, "flag"));
        entry.comment = normalize_external_text(json_value_string(detail, "comment"));
        entry.raw_event = normalize_external_text(json_value_string(detail, "raw_event"));
        entry.target_id = json_value_i64(detail, "target_id");
        entry.operator_id = json_value_i64(detail, "operator_id");
    }
    g_logs.push_back(entry);
    while (g_logs.size() > kMaxLogEntries) g_logs.pop_front();
    json j = log_entry_to_json(entry);
    push_to_frontend("new-log", j.dump());
}
// ============================================================
// Event Callback HTTP Server (receives OneBot events via POST)
// ============================================================

// Call OneBot API via WebSocket (for forward-ws mode where HTTP is unavailable)
static std::atomic<int> g_wsEcho{0};
static std::mutex g_wsResponseMutex;
static std::mutex g_wsApiCallMutex;
static std::map<std::string, json> g_wsResponses;

static json callApiViaWs(const std::string& action, const json& params = json::object(), int timeoutMs = 5000) {
    if (!g_ws.isConnected()) return json();
    std::unique_lock<std::mutex> apiLock(g_wsApiCallMutex);
    if (!g_ws.isConnected()) return json();
    startup_trace("ws api begin action=%s", action.c_str());
    int echo = ++g_wsEcho;
    std::string echoStr = std::to_string(echo);
    json req;
    req["action"] = action;
    req["params"] = params;
    req["echo"] = echoStr;
    if (!g_ws.sendText(req.dump())) {
        startup_trace("ws api send failed action=%s", action.c_str());
        return json();
    }
    
    // Wait for response
    auto start = std::chrono::steady_clock::now();
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_wsResponseMutex);
            auto it = g_wsResponses.find(echoStr);
            if (it != g_wsResponses.end()) {
                json resp = it->second;
                g_wsResponses.erase(it);
                startup_trace("ws api end action=%s", action.c_str());
                return resp;
            }
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeoutMs) {
            startup_trace("ws api timeout action=%s", action.c_str());
            return json();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static json callApiViaRuntimeWs(const std::shared_ptr<AccountRuntime>& rt, const std::string& action,
                                const json& params = json::object(), int timeoutMs = 5000) {
    if (!rt || !rt->ws || !rt->ws->isConnected()) return json();
    std::unique_lock<std::mutex> apiLock(rt->apiMutex);
    if (!rt->ws || !rt->ws->isConnected()) return json();
    int echo = ++rt->wsEcho;
    std::string echoStr = std::to_string(echo);
    json req;
    req["action"] = action;
    req["params"] = params;
    req["echo"] = echoStr;
    if (!rt->ws->sendText(req.dump())) return json();

    auto start = std::chrono::steady_clock::now();
    while (true) {
        {
            std::lock_guard<std::mutex> lock(rt->wsResponseMutex);
            auto it = rt->wsResponses.find(echoStr);
            if (it != rt->wsResponses.end()) {
                json resp = it->second;
                rt->wsResponses.erase(it);
                return resp;
            }
        }
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeoutMs) return json();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

static void setup_ws_handlers();
static int YUEX_PLUGIN_CALL plugin_send_private_msg(int64_t user_id, const char* message);
static int YUEX_PLUGIN_CALL plugin_send_group_msg(int64_t group_id, const char* message);
json call_onebot_api_bridge(const std::string& action, const json& params) {
    if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_ws.isConnected()) {
        return callApiViaWs(action, params);
    }
    return g_onebot.callApi(action, params);
}

static json onebot_failed_response(int code, const std::string& message) {
    json r;
    r["status"] = "failed";
    r["retcode"] = code;
    r["msg"] = message;
    r["wording"] = message;
    return r;
}

static bool normalize_send_message_action(const std::string& action, const json& params, std::string& outAction, json& outParams) {
    if (action != "send_message") return false;
    outParams = params.is_object() ? params : json::object();

    std::string messageType = outParams.value("message_type", outParams.value("type", ""));
    if (messageType == "group" || outParams.contains("group_id")) {
        outAction = "send_group_msg";
        if (!outParams.contains("group_id") && outParams.contains("target_id")) outParams["group_id"] = outParams["target_id"];
        if (!outParams.contains("group_id") && outParams.contains("target")) outParams["group_id"] = outParams["target"];
    } else {
        outAction = "send_private_msg";
        if (!outParams.contains("user_id") && outParams.contains("target_id")) outParams["user_id"] = outParams["target_id"];
        if (!outParams.contains("user_id") && outParams.contains("target")) outParams["user_id"] = outParams["target"];
    }

    outParams.erase("message_type");
    outParams.erase("type");
    outParams.erase("target_id");
    outParams.erase("target");
    return true;
}

static json call_onebot_api_bridge_compat(const std::string& action, const json& params) {
    std::string actualAction;
    json actualParams;
    if (normalize_send_message_action(action, params, actualAction, actualParams)) {
        if (actualAction == "send_group_msg" && !actualParams.contains("group_id")) return onebot_failed_response(400, "send_message missing group_id");
        if (actualAction == "send_private_msg" && !actualParams.contains("user_id")) return onebot_failed_response(400, "send_message missing user_id");
        return call_onebot_api_bridge(actualAction, actualParams);
    }
    return call_onebot_api_bridge(action, params);
}

static json call_onebot_api_bridge_for_account_compat(const std::string& accountRef, const std::string& action, const json& params) {
    std::string actualAction;
    json actualParams;
    if (normalize_send_message_action(action, params, actualAction, actualParams)) {
        if (actualAction == "send_group_msg" && !actualParams.contains("group_id")) return onebot_failed_response(400, "send_message missing group_id");
        if (actualAction == "send_private_msg" && !actualParams.contains("user_id")) return onebot_failed_response(400, "send_message missing user_id");
        return call_onebot_api_bridge_for_account(accountRef, actualAction, actualParams);
    }
    return call_onebot_api_bridge_for_account(accountRef, action, params);
}

static json sdk_normalize_response(const std::string& action, const json& raw) {
    bool ok = false;
    int code = -1;
    std::string message;
    if (raw.is_object()) {
        if (raw.contains("retcode")) {
            code = raw.value("retcode", -1);
            ok = (code == 0);
        } else if (raw.contains("status")) {
            std::string status = raw.value("status", "");
            ok = (status == "ok" || status == "success");
            code = ok ? 0 : -1;
        } else {
            ok = true;
            code = 0;
        }
        message = raw.value("message", raw.value("msg", raw.value("wording", ok ? "ok" : "failed")));
    } else if (!raw.is_null()) {
        ok = true;
        code = 0;
        message = "ok";
    } else {
        message = "empty response or timeout";
    }

    json r;
    r["ok"] = ok;
    r["code"] = code;
    r["message"] = message;
    r["action"] = action;
    if (raw.is_object() && raw.contains("data")) r["data"] = raw["data"];
    else r["data"] = raw;
    r["raw"] = raw;
    return r;
}

static void plugin_set_last_result_locked(const json& r) {
    g_pluginApiLastResult = r.dump();
    bool ok = r.value("ok", false);
    if (ok) {
        g_pluginApiLastError.clear();
    } else {
        g_pluginApiLastError = r.value("message", r.value("error", r.value("msg", "failed")));
    }
}

static void plugin_set_last_raw_locked(const std::string& action, const json& raw) {
    json n = sdk_normalize_response(action, raw);
    plugin_set_last_result_locked(n);
}

static void plugin_set_last_error_from_raw_locked(const std::string& action, const json& raw) {
    json n = sdk_normalize_response(action, raw);
    if (n.value("ok", false)) g_pluginApiLastError.clear();
    else g_pluginApiLastError = n.value("message", n.value("error", "failed"));
}

static json plugin_default_permissions_json() {
    return json::array({"events", "onebot_api", "send_message", "config", "data_dir"});
}

static std::vector<std::string> plugin_permissions_vector_from_json(const json& cfg, bool settingsAvailable) {
    json arr = plugin_default_permissions_json();
    if (cfg.is_object() && cfg.contains("permissions") && cfg["permissions"].is_array()) {
        arr = cfg["permissions"];
    }
    std::vector<std::string> out;
    for (auto& v : arr) {
        if (!v.is_string()) continue;
        std::string p = normalize_external_text(v.get<std::string>());
        if (!p.empty() && std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    if (settingsAvailable && std::find(out.begin(), out.end(), "settings") == out.end()) {
        out.push_back("settings");
    }
    return out;
}

static json plugin_permissions_json_from_vector(const std::vector<std::string>& perms) {
    json arr = json::array();
    for (auto& p : perms) arr.push_back(p);
    return arr;
}

static bool plugin_context_has_permission(const std::string& permission) {
    if (g_pluginCallContextId.empty()) return true;
    std::lock_guard<std::mutex> lock(g_pluginMutex);
    for (auto& p : g_plugins) {
        if (p.id != g_pluginCallContextId) continue;
        return std::find(p.permissions.begin(), p.permissions.end(), permission) != p.permissions.end();
    }
    json cfg = load_config_file("plugins/" + g_pluginCallContextId + ".json");
    auto perms = plugin_permissions_vector_from_json(cfg, false);
    return std::find(perms.begin(), perms.end(), permission) != perms.end();
}

static bool plugin_context_has_onebot_permission() {
    if (g_pluginCallContextId.empty()) return true;
    return plugin_context_has_permission("onebot_api");
}

static bool plugin_context_has_send_permission() {
    if (g_pluginCallContextId.empty()) return true;
    return plugin_context_has_permission("send_message") || plugin_context_has_permission("onebot_api");
}

static json plugin_permission_denied_response(const std::string& permission) {
    return onebot_failed_response(403, "plugin permission denied: " + permission);
}

static bool plugin_context_can_access_plugin_config(const std::string& pluginId, const std::string& permission) {
    if (g_pluginCallContextId.empty()) return true;
    if (pluginId != g_pluginCallContextId) return false;
    return plugin_context_has_permission(permission);
}

static std::string resolve_sdk_account_ref(const std::string& accountRef) {
    if (accountRef.empty() || accountRef == "*" || accountRef == "active" || accountRef == "current") {
        return g_activeAccountId;
    }
    for (auto& a : g_accounts) {
        if (a.id == accountRef || a.qq == accountRef) return a.id;
    }
    return "";
}

static json account_to_sdk_json(const AccountConfig& a) {
    json r;
    r["id"] = a.id;
    r["name"] = a.name;
    r["qq"] = a.qq;
    r["host"] = a.host;
    r["port"] = a.port;
    r["mode"] = a.mode;
    r["path"] = a.path;
    r["status"] = a.status;
    r["connected"] = (a.status == "online");
    r["auto_connect"] = a.autoConnect;
    auto rt = get_account_runtime(a.id);
    if (rt) {
        r["runtime_connected"] = rt->connected.load();
        r["runtime_connecting"] = rt->connecting.load();
        r["login_qq"] = rt->loginQq;
        r["nickname"] = rt->nickname;
        r["friend_count"] = (int)rt->friends.size();
        r["group_count"] = (int)rt->groups.size();
        r["latency"] = rt->latencyMs;
        r["effective_path"] = rt->path;
        r["ws_api_available"] = rt->wsApiAvailable.load();
        r["reconnect_attempts"] = rt->reconnectAttempts.load();
        r["next_reconnect_at"] = rt->nextReconnectAt.load();
        r["manual_stopped"] = rt->manualStopped.load();
    }
    if (!a.qq.empty()) r["avatar"] = "http://q1.qlogo.cn/g?b=qq&nk=" + a.qq + "&s=100";
    return r;
}

static json get_sdk_account_status_json(const std::string& accountRef) {
    std::string id = resolve_sdk_account_ref(accountRef);
    json r;
    r["ok"] = false;
    r["code"] = -1;
    r["message"] = "account not found";
    r["active_account_id"] = g_activeAccountId;
    r["connected"] = false;
    for (auto& a : g_accounts) {
        if (a.id == id) {
            r["ok"] = true;
            r["code"] = 0;
            r["message"] = "ok";
            auto rt = get_account_runtime(a.id);
            r["connected"] = rt ? rt->connected.load() : (a.status == "online" && a.id == g_activeAccountId && g_connected.load());
            r["account"] = account_to_sdk_json(a);
            return r;
        }
    }
    if (accountRef.empty() && !g_activeAccountId.empty()) r["message"] = "active account not found";
    return r;
}

json call_onebot_api_bridge_for_account(const std::string& accountRef, const std::string& action, const json& params) {
    std::string id = resolve_sdk_account_ref(accountRef);
    if (id.empty()) {
        return onebot_failed_response(404, "account not found");
    }
    auto rt = get_account_runtime(id);
    if (!rt) return onebot_failed_response(404, "account runtime not found");

    if ((rt->mode == "forward-ws" || rt->mode == "reverse-ws") && rt->ws && rt->ws->isConnected() && rt->wsApiAvailable.load()) {
        json resp = callApiViaRuntimeWs(rt, action, params);
        if (!resp.is_null()) return resp;
    }
    if (rt->http && rt->http->isConnected()) {
        return rt->http->callApi(action, params);
    }

    bool isActiveGlobal = (id == g_activeAccountId);
    bool runtimeConnected = rt->connected.load();
    if (isActiveGlobal && runtimeConnected && g_connected.load()) {
        return call_onebot_api_bridge(action, params);
    }
    if (!runtimeConnected) {
        return onebot_failed_response(503, "account is not connected");
    }
    return onebot_failed_response(503, "account transport is not ready");
}

static void publish_connection_info(const std::string& mode, const std::string& host, int port) {
    try {
    startup_trace("publish_connection_info begin mode=%s", mode.c_str());
    json info;
    if ((mode == "forward-ws" || mode == "reverse-ws") && g_ws.isConnected()) {
        info = callApiViaWs("get_login_info", json::object(), 3000);
        if (!info.contains("data") || info.is_null()) {
            startup_trace("ws get_login_info failed, trying http fallback");
            g_onebot.setConfig(host, port, g_token);
            auto httpInfo = g_onebot.getLoginInfo();
            if (httpInfo.contains("data") && httpInfo["data"].is_object()) {
                info = httpInfo;
                startup_trace("http fallback get_login_info ok");
            }
        }
    } else {
        info = g_onebot.getLoginInfo();
        if (!info.contains("data")) info = g_onebot.get_login_info();
    }

    startup_trace("publish info dump: %s", info.dump().substr(0, 300).c_str());

    std::string qq;
    std::string name = "YuexBot";
    if (info.contains("data") && info["data"].is_object()) {
        auto d = info["data"];
        if (d.contains("user_id")) {
            qq = json_value_string(d, "user_id");
        }
        std::string nick = json_value_string(d, "nickname");
        if (!nick.empty()) name = nick;
    }
    startup_trace("publish resolved qq=%s name=%s from api=%d", qq.c_str(), name.c_str(), info.contains("data")?1:0);

    if (qq.empty()) {
        // Fallback: try to find account by active account id, then by host/port
        for (auto& a : g_accounts) {
            if (a.id == g_activeAccountId && !a.qq.empty()) {
                qq = a.qq; if (!a.name.empty()) name = a.name; break;
            }
        }
        if (qq.empty()) {
        for (auto& a : g_accounts) {
            if (a.host == host && a.port == port && !a.qq.empty()) {
                qq = a.qq;
                if (!a.name.empty()) name = a.name;
                break;
            }
        }
        }
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::string accountId = resolve_account_id_for_login(qq, host, port);
        if (!accountId.empty()) g_activeAccountId = accountId;
        bool found = false;
        for (auto& a : g_accounts) {
            if (a.id == g_activeAccountId || (!qq.empty() && a.qq == qq) || (a.host == host && a.port == port)) {
                if (!qq.empty()) a.qq = qq;
                if (!name.empty()) a.name = name;
                a.host = host;
                a.port = port;
                a.mode = mode;
                a.status = "online";
                found = true;
                if (g_activeAccountId.empty()) g_activeAccountId = a.id;
                break;
            }
        }
        if (!found && !qq.empty()) {
            AccountConfig a;
            a.id = "acc_" + qq;
            a.name = name;
            a.qq = qq;
            a.host = host;
            a.port = port;
            a.token = g_token;
            a.mode = mode;
            a.status = "online";
            g_accounts.push_back(a);
            g_activeAccountId = a.id;
        }
        sync_account_runtimes_from_configs();
        save_accounts_to_yaml();
    }
    update_account_runtime_login(g_activeAccountId, qq, name);
    update_account_runtime_status(g_activeAccountId, "online");
    if (!g_activeAccountId.empty()) {
        auto rt = get_account_runtime(g_activeAccountId);
        if (rt) {
            rt->mode = mode;
            rt->host = host;
            rt->port = port;
            rt->path = (mode == "forward-ws") ? g_wsPath : rt->path;
            rt->wsApiAvailable = ((mode == "forward-ws" || mode == "reverse-ws") && g_ws.isConnected());
        }
    }

    json fr = call_onebot_api_bridge("get_friend_list");
    json gr = call_onebot_api_bridge("get_group_list");
    cache_account_lists(g_activeAccountId, fr, gr);

    if (!qq.empty()) add_log("system", "", "连接成功，账号 QQ " + qq + " (" + name + ")");
    else add_log("system", "", "connected, but QQ account was not resolved");

    json status;
    status["connected"] = true;
    status["qq"] = qq;
    status["name"] = name;
    status["account_id"] = g_activeAccountId;
    status["mode"] = mode;
    status["host"] = host;
    status["port"] = port;
    status["path"] = (mode == "forward-ws") ? g_wsPath : "";
    status["friends"] = (int)g_friends.size();
    status["groups"] = (int)g_groups.size();
    push_to_frontend("status-changed", status.dump());
    push_to_frontend("accounts-updated", accounts_to_json().dump());
    startup_trace("publish_connection_info ok mode=%s account=%s", mode.c_str(), g_activeAccountId.c_str());
    } catch (const std::exception& e) {
        add_log("system", "", std::string("运行异常: ") + e.what());
        startup_trace("publish_connection_info exception=%s", e.what());
    } catch (...) {
        add_log("system", "", "连接信息刷新失败: unknown");
        startup_trace("publish_connection_info unknown exception");
    }
}

static const char* YUEX_PLUGIN_CALL plugin_call_onebot_api(const char* action, const char* params_json) {
    if (!plugin_context_has_onebot_permission()) {
        std::lock_guard<std::mutex> lock(g_pluginApiMutex);
        json r = plugin_permission_denied_response("onebot_api");
        g_pluginApiLastResult = r.dump();
        plugin_set_last_error_from_raw_locked(action ? action : "", r);
        return g_pluginApiLastResult.c_str();
    }
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    try {
        json params = json::object();
        if (params_json && std::strlen(params_json) > 0) params = json::parse(params_json);
        json r = call_onebot_api_bridge_compat(action ? action : "", params);
        g_pluginApiLastResult = r.dump();
        plugin_set_last_error_from_raw_locked(action ? action : "", r);
    } catch (...) {
        json r = onebot_failed_response(-1, "plugin api exception");
        g_pluginApiLastResult = r.dump();
        plugin_set_last_error_from_raw_locked(action ? action : "", r);
    }
    return g_pluginApiLastResult.c_str();
}

static const char* plugin_store_json(const json& value) {
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    g_pluginApiLastResult = value.dump();
    return g_pluginApiLastResult.c_str();
}

static json parse_plugin_params_json(const char* params_json) {
    if (!params_json || std::strlen(params_json) == 0) return json::object();
    auto parsed = json::parse(params_json);
    return parsed.is_null() ? json::object() : parsed;
}

static const char* YUEX_PLUGIN_CALL plugin_get_sdk_info() {
    json r;
    r["name"] = "YuexBot Plugin SDK";
    r["abi_version"] = YUEX_PLUGIN_ABI_VERSION;
    r["framework"] = "YuexBot";
    r["framework_version"] = kYuexBotVersion;
    r["encoding"] = "UTF-8";
    r["onebot"] = "OneBot 11";
    r["features"] = {
        "raw_onebot_api",
        "normalized_onebot_api",
        "account_aware_api",
        "message_events",
        "notice_events",
        "request_events",
        "meta_events",
        "normalized_event_fields",
        "plugin_config",
        "plugin_data_dir",
        "account_aware_group_admin_api",
        "account_aware_group_file_api",
        "connection_diagnostics",
        "last_result",
        "last_error",
        "plugin_permissions",
        "event_filter"
    };
    r["onebot11_actions"] = {
        "send_private_msg",
        "send_group_msg",
        "send_msg",
        "send_message",
        "delete_msg",
        "get_msg",
        "get_forward_msg",
        "send_like",
        "set_group_kick",
        "set_group_ban",
        "set_group_anonymous_ban",
        "set_group_whole_ban",
        "set_group_admin",
        "set_group_anonymous",
        "set_group_card",
        "set_group_name",
        "set_group_leave",
        "set_group_special_title",
        "set_friend_add_request",
        "set_group_add_request",
        "get_login_info",
        "get_stranger_info",
        "get_friend_list",
        "get_group_info",
        "get_group_list",
        "get_group_member_info",
        "get_group_member_list",
        "get_group_honor_info",
        "get_cookies",
        "get_csrf_token",
        "get_credentials",
        "get_record",
        "get_image",
        "can_send_image",
        "can_send_record",
        "get_status",
        "get_version_info",
        "set_restart",
        "clean_cache",
        "get_group_root_files",
        "get_group_files",
        "upload_group_file",
        "delete_group_file",
        "move_group_file",
        "create_group_folder",
        "delete_group_folder",
        "rename_group_folder",
        "_send_group_notice",
        "_get_group_notice",
        "get_custom_face_url_list",
        "get_group_essence_msg_list"
    };
    r["typed_sdk_actions"] = {
        "get_login_info",
        "get_status",
        "get_friend_list",
        "get_group_list",
        "get_group_info",
        "get_group_member_info",
        "get_group_member_list",
        "send_private_msg",
        "send_group_msg",
        "send_msg",
        "send_message",
        "delete_msg",
        "get_msg",
        "get_forward_msg",
        "set_group_ban",
        "set_group_kick",
        "set_group_admin",
        "set_group_name",
        "set_group_whole_ban",
        "set_group_leave",
        "set_group_card",
        "set_group_special_title",
        "set_friend_add_request",
        "set_group_add_request",
        "get_group_root_files",
        "get_group_files",
        "upload_group_file",
        "delete_group_file",
        "move_group_file",
        "create_group_folder",
        "delete_group_folder",
        "rename_group_folder",
        "send_like",
        "_send_group_notice",
        "_get_group_notice",
        "get_custom_face_url_list",
        "get_group_essence_msg_list",
        "ocr_image",
        "get_rkey",
        "get_clientkey",
        "get_group_album_list",
        "get_group_album_media_list",
        "get_group_system_msg",
        "get_group_ignore_add_request",
        "get_group_file_url",
        "download_file",
        "get_ai_characters",
        "send_group_ai_record",
        "get_version_info",
        "get_stranger_info",
        "get_group_honor_info",
        "get_record",
        "get_image",
        "can_send_image",
        "can_send_record",
        "get_cookies",
        "get_csrf_token",
        "get_credentials",
        "get_group_shut_list",
        "get_group_at_all_remain",
        "set_group_portrait",
        "upload_private_file",
        "fetch_custom_face"
    };
    r["generic_extension_actions"] = {
    };
    r["event_support"] = {
        {"message", "parsed and dispatched"},
        {"notice", "parsed and dispatched; specialized UI is partial"},
        {"request", "parsed and dispatched; approve/reject UI for add requests"},
        {"meta_event", "parsed and dispatched"}
    };
    r["message_segment_support"] = {
        {"text", "preview and raw detail"},
        {"reply", "compact preview and raw detail"},
        {"at", "compact preview and raw detail"},
        {"image", "compact preview and raw detail; rich preview pending"},
        {"record", "compact preview and raw detail; playback pending"},
        {"video", "compact preview and raw detail; playback pending"},
        {"file", "compact preview and raw detail; file panel pending"},
        {"json", "raw detail; rich card preview pending"},
        {"markdown", "raw detail; rich markdown preview pending"},
        {"ark", "raw detail; rich card preview pending"},
        {"forward", "raw detail; forward tree preview pending"}
    };
    r["notes"] = "call_onebot_api keeps native OneBot response; call_onebot_api_ex returns YuexBot normalized response; call_onebot_api_as_ex can call NapCat/LLBot extension actions that do not yet have typed SDK wrappers; event_json includes YuexBot normalized fields since ABI v7; ABI v8 adds more account-aware OneBot wrappers; ABI v9 adds last_result/last_error, permissions and event filters; ABI v10 adds typed wrappers for version, stranger, honor, record/image and can_send capability APIs";
    return plugin_store_json(r);
}

static const char* YUEX_PLUGIN_CALL plugin_get_framework_version() {
    json r;
    r["name"] = "YuexBot";
    r["version"] = kYuexBotVersion;
    r["abi_version"] = YUEX_PLUGIN_ABI_VERSION;
    r["client_mode"] = g_connMode;
    return plugin_store_json(r);
}

static const char* YUEX_PLUGIN_CALL plugin_get_account_status(const char* account_id_or_qq) {
    return plugin_store_json(get_sdk_account_status_json(account_id_or_qq ? account_id_or_qq : ""));
}

static const char* YUEX_PLUGIN_CALL plugin_call_onebot_api_as(const char* account_id_or_qq, const char* action, const char* params_json) {
    if (!plugin_context_has_onebot_permission()) {
        std::lock_guard<std::mutex> lock(g_pluginApiMutex);
        json r = plugin_permission_denied_response("onebot_api");
        g_pluginApiLastResult = r.dump();
        plugin_set_last_error_from_raw_locked(action ? action : "", r);
        return g_pluginApiLastResult.c_str();
    }
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    try {
        json params = parse_plugin_params_json(params_json);
        json r = call_onebot_api_bridge_for_account_compat(account_id_or_qq ? account_id_or_qq : "", action ? action : "", params);
        g_pluginApiLastResult = r.dump();
        plugin_set_last_error_from_raw_locked(action ? action : "", r);
    } catch (...) {
        json r = onebot_failed_response(-1, "plugin account api exception");
        g_pluginApiLastResult = r.dump();
        plugin_set_last_error_from_raw_locked(action ? action : "", r);
    }
    return g_pluginApiLastResult.c_str();
}

static const char* YUEX_PLUGIN_CALL plugin_call_onebot_api_ex(const char* action, const char* params_json) {
    if (!plugin_context_has_onebot_permission()) {
        std::lock_guard<std::mutex> lock(g_pluginApiMutex);
        plugin_set_last_raw_locked(action ? action : "", plugin_permission_denied_response("onebot_api"));
        return g_pluginApiLastResult.c_str();
    }
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    try {
        json params = parse_plugin_params_json(params_json);
        json raw = call_onebot_api_bridge_compat(action ? action : "", params);
        plugin_set_last_raw_locked(action ? action : "", raw);
    } catch (...) {
        plugin_set_last_raw_locked(action ? action : "", onebot_failed_response(-1, "plugin api exception"));
    }
    return g_pluginApiLastResult.c_str();
}

static const char* YUEX_PLUGIN_CALL plugin_call_onebot_api_as_ex(const char* account_id_or_qq, const char* action, const char* params_json) {
    if (!plugin_context_has_onebot_permission()) {
        std::lock_guard<std::mutex> lock(g_pluginApiMutex);
        plugin_set_last_raw_locked(action ? action : "", plugin_permission_denied_response("onebot_api"));
        return g_pluginApiLastResult.c_str();
    }
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    try {
        json params = parse_plugin_params_json(params_json);
        json raw = call_onebot_api_bridge_for_account_compat(account_id_or_qq ? account_id_or_qq : "", action ? action : "", params);
        json r = sdk_normalize_response(action ? action : "", raw);
        r["account"] = get_sdk_account_status_json(account_id_or_qq ? account_id_or_qq : "");
        plugin_set_last_result_locked(r);
    } catch (...) {
        plugin_set_last_raw_locked(action ? action : "", onebot_failed_response(-1, "plugin account api exception"));
    }
    return g_pluginApiLastResult.c_str();
}

static const char* YUEX_PLUGIN_CALL plugin_get_friend_list_as(const char* account_id_or_qq) {
    std::string id = resolve_sdk_account_ref(account_id_or_qq ? account_id_or_qq : "");
    if (!id.empty() && id == g_activeAccountId && g_connected.load()) {
        return plugin_store_json(call_onebot_api_bridge("get_friend_list"));
    }
    json r;
    r["status"] = "failed";
    r["retcode"] = 404;
    r["msg"] = "account friend cache not found";
    auto it = g_accountFriends.find(id);
    if (it != g_accountFriends.end()) {
        r["status"] = "ok";
        r["retcode"] = 0;
        r["data"] = it->second;
        r["msg"] = "ok";
    }
    return plugin_store_json(r);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_list_as(const char* account_id_or_qq) {
    std::string id = resolve_sdk_account_ref(account_id_or_qq ? account_id_or_qq : "");
    if (!id.empty() && id == g_activeAccountId && g_connected.load()) {
        return plugin_store_json(call_onebot_api_bridge("get_group_list"));
    }
    json r;
    r["status"] = "failed";
    r["retcode"] = 404;
    r["msg"] = "account group cache not found";
    auto it = g_accountGroups.find(id);
    if (it != g_accountGroups.end()) {
        r["status"] = "ok";
        r["retcode"] = 0;
        r["data"] = it->second;
        r["msg"] = "ok";
    }
    return plugin_store_json(r);
}

static const char* YUEX_PLUGIN_CALL plugin_get_avatar_url(int64_t user_id, int32_t size) {
    if (size <= 0) size = 100;
    if (size != 40 && size != 100 && size != 140 && size != 640) size = 100;
    json r;
    r["user_id"] = user_id;
    r["size"] = size;
    r["url"] = "http://q1.qlogo.cn/g?b=qq&nk=" + std::to_string(user_id) + "&s=" + std::to_string(size);
    return plugin_store_json(r);
}

static int plugin_retcode_ok(const json& r);

static int plugin_return_bool_result(const std::string& action, const json& r) {
    {
        std::lock_guard<std::mutex> lock(g_pluginApiMutex);
        plugin_set_last_raw_locked(action, r);
    }
    return plugin_retcode_ok(r);
}

static const char* YUEX_PLUGIN_CALL plugin_get_login_info_as(const char* account_id_or_qq) {
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_login_info", json::object()));
}

static const char* YUEX_PLUGIN_CALL plugin_get_status_as(const char* account_id_or_qq) {
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_status", json::object()));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_member_list_as(const char* account_id_or_qq, int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_group_member_list", p));
}

static int YUEX_PLUGIN_CALL plugin_send_private_msg_as(const char* account_id_or_qq, int64_t user_id, const char* message) {
    if (!plugin_context_has_send_permission()) {
        return plugin_return_bool_result("send_private_msg", plugin_permission_denied_response("send_message"));
    }
    json p; p["user_id"] = user_id; p["message"] = message ? message : "";
    return plugin_return_bool_result("send_private_msg", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "send_private_msg", p));
}

static int YUEX_PLUGIN_CALL plugin_send_group_msg_as(const char* account_id_or_qq, int64_t group_id, const char* message) {
    if (!plugin_context_has_send_permission()) {
        return plugin_return_bool_result("send_group_msg", plugin_permission_denied_response("send_message"));
    }
    json p; p["group_id"] = group_id; p["message"] = message ? message : "";
    return plugin_return_bool_result("send_group_msg", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "send_group_msg", p));
}

static int YUEX_PLUGIN_CALL plugin_send_msg_as(const char* account_id_or_qq, int msg_type, int64_t target_id, const char* message) {
    return msg_type == 1 ? plugin_send_group_msg_as(account_id_or_qq, target_id, message)
                         : plugin_send_private_msg_as(account_id_or_qq, target_id, message);
}

static int YUEX_PLUGIN_CALL plugin_delete_msg_as(const char* account_id_or_qq, int32_t message_id) {
    json p; p["message_id"] = message_id;
    return plugin_return_bool_result("delete_msg", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "delete_msg", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_msg_as(const char* account_id_or_qq, int32_t message_id) {
    json p; p["message_id"] = message_id;
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_msg", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_forward_msg_as(const char* account_id_or_qq, const char* id) {
    json p; p["id"] = id ? id : "";
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_forward_msg", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_info_as(const char* account_id_or_qq, int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_group_info", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_member_info_as(const char* account_id_or_qq, int64_t group_id, int64_t user_id) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id;
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_group_member_info", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_ban_as(const char* account_id_or_qq, int64_t group_id, int64_t user_id, int32_t duration) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["duration"] = duration;
    return plugin_return_bool_result("set_group_ban", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_ban", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_kick_as(const char* account_id_or_qq, int64_t group_id, int64_t user_id, int reject_add_request) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["reject_add_request"] = reject_add_request != 0;
    return plugin_return_bool_result("set_group_kick", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_kick", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_admin_as(const char* account_id_or_qq, int64_t group_id, int64_t user_id, int enable) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["enable"] = enable != 0;
    return plugin_return_bool_result("set_group_admin", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_admin", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_name_as(const char* account_id_or_qq, int64_t group_id, const char* group_name) {
    json p; p["group_id"] = group_id; p["group_name"] = group_name ? group_name : "";
    return plugin_return_bool_result("set_group_name", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_name", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_whole_ban_as(const char* account_id_or_qq, int64_t group_id, int enable) {
    json p; p["group_id"] = group_id; p["enable"] = enable != 0;
    return plugin_return_bool_result("set_group_whole_ban", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_whole_ban", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_card_as(const char* account_id_or_qq, int64_t group_id, int64_t user_id, const char* card) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["card"] = card ? card : "";
    return plugin_return_bool_result("set_group_card", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_card", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_leave_as(const char* account_id_or_qq, int64_t group_id, int is_dismiss) {
    json p; p["group_id"] = group_id; p["is_dismiss"] = is_dismiss != 0;
    return plugin_return_bool_result("set_group_leave", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_leave", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_special_title_as(const char* account_id_or_qq, int64_t group_id, int64_t user_id, const char* special_title) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["special_title"] = special_title ? special_title : "";
    return plugin_return_bool_result("set_group_special_title", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_special_title", p));
}

static int YUEX_PLUGIN_CALL plugin_set_friend_add_request_as(const char* account_id_or_qq, const char* flag, int approve, const char* remark) {
    json p; p["flag"] = flag ? flag : ""; p["approve"] = approve != 0;
    if (remark && *remark) p["remark"] = remark;
    return plugin_return_bool_result("set_friend_add_request", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_friend_add_request", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_add_request_as(const char* account_id_or_qq, const char* flag, const char* sub_type, int approve, const char* reason) {
    json p; p["flag"] = flag ? flag : ""; p["sub_type"] = sub_type ? sub_type : ""; p["approve"] = approve != 0;
    if (reason && *reason) p["reason"] = reason;
    return plugin_return_bool_result("set_group_add_request", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_add_request", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_root_files_as(const char* account_id_or_qq, int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_group_root_files", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_files_as(const char* account_id_or_qq, int64_t group_id, const char* folder_id, int32_t start_index) {
    json p; p["group_id"] = group_id; p["start_index"] = start_index;
    if (folder_id && *folder_id) p["folder_id"] = folder_id;
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_group_files", p));
}

static int YUEX_PLUGIN_CALL plugin_upload_group_file_as(const char* account_id_or_qq, int64_t group_id, const char* file, const char* name) {
    json p; p["group_id"] = group_id; p["file"] = file ? file : "";
    if (name && *name) p["name"] = name;
    return plugin_return_bool_result("upload_group_file", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "upload_group_file", p));
}

static int YUEX_PLUGIN_CALL plugin_delete_group_file_as(const char* account_id_or_qq, int64_t group_id, const char* file_id, int32_t busid) {
    json p; p["group_id"] = group_id; p["file_id"] = file_id ? file_id : ""; p["busid"] = busid;
    return plugin_return_bool_result("delete_group_file", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "delete_group_file", p));
}

static int YUEX_PLUGIN_CALL plugin_move_group_file_as(const char* account_id_or_qq, int64_t group_id, const char* file_id, const char* parent_folder, const char* target_folder) {
    json p; p["group_id"] = group_id; p["file_id"] = file_id ? file_id : "";
    p["parent_folder"] = parent_folder ? parent_folder : "";
    p["target_folder"] = target_folder ? target_folder : "";
    return plugin_return_bool_result("move_group_file", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "move_group_file", p));
}

static int YUEX_PLUGIN_CALL plugin_create_group_folder_as(const char* account_id_or_qq, int64_t group_id, const char* folder_name) {
    json p; p["group_id"] = group_id; p["folder_name"] = folder_name ? folder_name : "";
    return plugin_return_bool_result("create_group_folder", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "create_group_folder", p));
}

static int YUEX_PLUGIN_CALL plugin_delete_group_folder_as(const char* account_id_or_qq, int64_t group_id, const char* folder_id) {
    json p; p["group_id"] = group_id; p["folder_id"] = folder_id ? folder_id : "";
    return plugin_return_bool_result("delete_group_folder", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "delete_group_folder", p));
}

static int YUEX_PLUGIN_CALL plugin_rename_group_folder_as(const char* account_id_or_qq, int64_t group_id, const char* folder_id, const char* new_folder_name) {
    json p; p["group_id"] = group_id; p["folder_id"] = folder_id ? folder_id : ""; p["new_folder_name"] = new_folder_name ? new_folder_name : "";
    return plugin_return_bool_result("rename_group_folder", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "rename_group_folder", p));
}

static int YUEX_PLUGIN_CALL plugin_send_like_as(const char* account_id_or_qq, int64_t user_id, int32_t times) {
    json p; p["user_id"] = user_id; p["times"] = times;
    return plugin_return_bool_result("send_like", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "send_like", p));
}

static int YUEX_PLUGIN_CALL plugin_send_group_notice_as(const char* account_id_or_qq, int64_t group_id, const char* content, const char* image) {
    json p; p["group_id"] = group_id; p["content"] = content ? content : "";
    if (image && *image) p["image"] = image;
    return plugin_return_bool_result("_send_group_notice", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "_send_group_notice", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_notice_as(const char* account_id_or_qq, int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "_get_group_notice", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_custom_face_url_list_as(const char* account_id_or_qq) {
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_custom_face_url_list", json::object()));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_essence_msg_list_as(const char* account_id_or_qq, int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "get_group_essence_msg_list", p));
}

static const char* plugin_call_extension_json_as(const char* account_id_or_qq, const std::string& action, const json& params) {
    if (!plugin_context_has_onebot_permission()) {
        return plugin_store_json(plugin_permission_denied_response("onebot_api"));
    }
    return plugin_store_json(call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", action, params));
}

static const char* YUEX_PLUGIN_CALL plugin_ocr_image_as(const char* account_id_or_qq, const char* image) {
    json p; p["image"] = image ? image : "";
    return plugin_call_extension_json_as(account_id_or_qq, "ocr_image", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_rkey_as(const char* account_id_or_qq) {
    return plugin_call_extension_json_as(account_id_or_qq, "get_rkey", json::object());
}

static const char* YUEX_PLUGIN_CALL plugin_get_clientkey_as(const char* account_id_or_qq) {
    return plugin_call_extension_json_as(account_id_or_qq, "get_clientkey", json::object());
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_album_list_as(const char* account_id_or_qq, int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_call_extension_json_as(account_id_or_qq, "get_group_album_list", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_album_media_list_as(const char* account_id_or_qq, int64_t group_id, const char* album_id) {
    json p; p["group_id"] = group_id; p["album_id"] = album_id ? album_id : "";
    return plugin_call_extension_json_as(account_id_or_qq, "get_group_album_media_list", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_system_msg_as(const char* account_id_or_qq) {
    return plugin_call_extension_json_as(account_id_or_qq, "get_group_system_msg", json::object());
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_ignore_add_request_as(const char* account_id_or_qq) {
    return plugin_call_extension_json_as(account_id_or_qq, "get_group_ignore_add_request", json::object());
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_file_url_as(const char* account_id_or_qq, int64_t group_id, const char* file_id, int32_t busid) {
    json p; p["group_id"] = group_id; p["file_id"] = file_id ? file_id : ""; p["busid"] = busid;
    return plugin_call_extension_json_as(account_id_or_qq, "get_group_file_url", p);
}

static const char* YUEX_PLUGIN_CALL plugin_download_file_as(const char* account_id_or_qq, const char* url, const char* headers_json) {
    json p; p["url"] = url ? url : "";
    if (headers_json && *headers_json) {
        try { p["headers"] = json::parse(headers_json); } catch (...) { p["headers"] = headers_json; }
    }
    return plugin_call_extension_json_as(account_id_or_qq, "download_file", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_ai_characters_as(const char* account_id_or_qq, int64_t group_id, const char* chat_type) {
    json p; if (group_id > 0) p["group_id"] = group_id; if (chat_type && *chat_type) p["chat_type"] = chat_type;
    return plugin_call_extension_json_as(account_id_or_qq, "get_ai_characters", p);
}

static int YUEX_PLUGIN_CALL plugin_send_group_ai_record_as(const char* account_id_or_qq, int64_t group_id, const char* character, const char* text) {
    if (!plugin_context_has_send_permission()) {
        return plugin_return_bool_result("send_group_ai_record", plugin_permission_denied_response("send_message"));
    }
    json p; p["group_id"] = group_id; p["character"] = character ? character : ""; p["text"] = text ? text : "";
    return plugin_return_bool_result("send_group_ai_record", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "send_group_ai_record", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_version_info_as(const char* account_id_or_qq) {
    return plugin_call_extension_json_as(account_id_or_qq, "get_version_info", json::object());
}

static const char* YUEX_PLUGIN_CALL plugin_get_stranger_info_as(const char* account_id_or_qq, int64_t user_id, int no_cache) {
    json p; p["user_id"] = user_id; p["no_cache"] = no_cache != 0;
    return plugin_call_extension_json_as(account_id_or_qq, "get_stranger_info", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_honor_info_as(const char* account_id_or_qq, int64_t group_id, const char* type) {
    json p; p["group_id"] = group_id; p["type"] = (type && *type) ? type : "all";
    return plugin_call_extension_json_as(account_id_or_qq, "get_group_honor_info", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_record_as(const char* account_id_or_qq, const char* file, const char* out_format) {
    json p; p["file"] = file ? file : "";
    if (out_format && *out_format) p["out_format"] = out_format;
    return plugin_call_extension_json_as(account_id_or_qq, "get_record", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_image_as(const char* account_id_or_qq, const char* file) {
    json p; p["file"] = file ? file : "";
    return plugin_call_extension_json_as(account_id_or_qq, "get_image", p);
}

static const char* YUEX_PLUGIN_CALL plugin_can_send_image_as(const char* account_id_or_qq) {
    return plugin_call_extension_json_as(account_id_or_qq, "can_send_image", json::object());
}

static const char* YUEX_PLUGIN_CALL plugin_can_send_record_as(const char* account_id_or_qq) {
    return plugin_call_extension_json_as(account_id_or_qq, "can_send_record", json::object());
}

static const char* YUEX_PLUGIN_CALL plugin_get_cookies_as(const char* account_id_or_qq, const char* domain) {
    json p; if (domain && *domain) p["domain"] = domain;
    return plugin_call_extension_json_as(account_id_or_qq, "get_cookies", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_csrf_token_as(const char* account_id_or_qq) {
    return plugin_call_extension_json_as(account_id_or_qq, "get_csrf_token", json::object());
}

static const char* YUEX_PLUGIN_CALL plugin_get_credentials_as(const char* account_id_or_qq, const char* domain) {
    json p; if (domain && *domain) p["domain"] = domain;
    return plugin_call_extension_json_as(account_id_or_qq, "get_credentials", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_shut_list_as(const char* account_id_or_qq, int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_call_extension_json_as(account_id_or_qq, "get_group_shut_list", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_at_all_remain_as(const char* account_id_or_qq, int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_call_extension_json_as(account_id_or_qq, "get_group_at_all_remain", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_portrait_as(const char* account_id_or_qq, int64_t group_id, const char* file, int cache) {
    json p; p["group_id"] = group_id; p["file"] = file ? file : ""; p["cache"] = cache != 0;
    return plugin_return_bool_result("set_group_portrait", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "set_group_portrait", p));
}

static int YUEX_PLUGIN_CALL plugin_upload_private_file_as(const char* account_id_or_qq, int64_t user_id, const char* file, const char* name) {
    json p; p["user_id"] = user_id; p["file"] = file ? file : ""; if (name && *name) p["name"] = name;
    return plugin_return_bool_result("upload_private_file", call_onebot_api_bridge_for_account(account_id_or_qq ? account_id_or_qq : "", "upload_private_file", p));
}

static const char* YUEX_PLUGIN_CALL plugin_fetch_custom_face_as(const char* account_id_or_qq, int32_t count) {
    json p; if (count > 0) p["count"] = count;
    return plugin_call_extension_json_as(account_id_or_qq, "fetch_custom_face", p);
}

static int plugin_retcode_ok(const json& r);

static int plugin_call_bool(const std::string& action, const json& params = json::object()) {
    if (!plugin_context_has_onebot_permission()) {
        return plugin_return_bool_result(action, plugin_permission_denied_response("onebot_api"));
    }
    return plugin_return_bool_result(action, call_onebot_api_bridge(action, params));
}

static const char* YUEX_PLUGIN_CALL plugin_get_active_account() {
    json r;
    r["account_id"] = g_activeAccountId;
    r["connected"] = g_connected.load();
    r["mode"] = g_connMode;
    r["host"] = g_host;
    r["port"] = g_port;
    for (auto& a : g_accounts) {
        if (a.id == g_activeAccountId) {
            r["account"] = {
                {"id", a.id}, {"name", a.name}, {"qq", a.qq}, {"mode", a.mode},
                {"host", a.host}, {"port", a.port}, {"status", a.status}
            };
            break;
        }
    }
    return plugin_store_json(r);
}

static const char* YUEX_PLUGIN_CALL plugin_get_accounts() {
    return plugin_store_json(accounts_to_json());
}

static const char* YUEX_PLUGIN_CALL plugin_get_login_info() {
    return plugin_store_json(call_onebot_api_bridge("get_login_info"));
}

static const char* YUEX_PLUGIN_CALL plugin_get_status() {
    return plugin_store_json(call_onebot_api_bridge("get_status"));
}

static const char* YUEX_PLUGIN_CALL plugin_get_friend_list() {
    return plugin_store_json(call_onebot_api_bridge("get_friend_list"));
}

static int YUEX_PLUGIN_CALL plugin_delete_friend(int64_t user_id) {
    json p; p["user_id"] = user_id;
    return plugin_call_bool("delete_friend", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_list() {
    return plugin_store_json(call_onebot_api_bridge("get_group_list"));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_member_list(int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge("get_group_member_list", p));
}

static int plugin_retcode_ok(const json& r) {
    if (r.contains("retcode")) return r.value("retcode", -1) == 0 ? 1 : 0;
    if (r.contains("status")) return r.value("status", "") == "ok" ? 1 : 0;
    return r.is_object() ? 1 : 0;
}

static int YUEX_PLUGIN_CALL plugin_send_msg(int msg_type, int64_t target_id, const char* message) {
    return msg_type == 1 ? plugin_send_group_msg(target_id, message)
                         : plugin_send_private_msg(target_id, message);
}

static int YUEX_PLUGIN_CALL plugin_send_private_msg(int64_t user_id, const char* message) {
    if (!plugin_context_has_send_permission()) {
        return plugin_return_bool_result("send_private_msg", plugin_permission_denied_response("send_message"));
    }
    json p; p["user_id"] = user_id; p["message"] = message ? message : "";
    return plugin_return_bool_result("send_private_msg", call_onebot_api_bridge("send_private_msg", p));
}

static int YUEX_PLUGIN_CALL plugin_send_group_msg(int64_t group_id, const char* message) {
    if (!plugin_context_has_send_permission()) {
        return plugin_return_bool_result("send_group_msg", plugin_permission_denied_response("send_message"));
    }
    json p; p["group_id"] = group_id; p["message"] = message ? message : "";
    return plugin_return_bool_result("send_group_msg", call_onebot_api_bridge("send_group_msg", p));
}

static int YUEX_PLUGIN_CALL plugin_delete_msg(int32_t message_id) {
    json p; p["message_id"] = message_id;
    return plugin_call_bool("delete_msg", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_msg(int32_t message_id) {
    json p; p["message_id"] = message_id;
    return plugin_store_json(call_onebot_api_bridge("get_msg", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_forward_msg(const char* id) {
    json p; p["id"] = id ? id : "";
    return plugin_store_json(call_onebot_api_bridge("get_forward_msg", p));
}

static int YUEX_PLUGIN_CALL plugin_set_group_ban(int64_t group_id, int64_t user_id, int32_t duration) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["duration"] = duration;
    return plugin_call_bool("set_group_ban", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_kick(int64_t group_id, int64_t user_id, int reject_add_request) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["reject_add_request"] = reject_add_request != 0;
    return plugin_call_bool("set_group_kick", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_admin(int64_t group_id, int64_t user_id, int enable) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["enable"] = enable != 0;
    return plugin_call_bool("set_group_admin", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_name(int64_t group_id, const char* group_name) {
    json p; p["group_id"] = group_id; p["group_name"] = group_name ? group_name : "";
    return plugin_call_bool("set_group_name", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_whole_ban(int64_t group_id, int enable) {
    json p; p["group_id"] = group_id; p["enable"] = enable != 0;
    return plugin_call_bool("set_group_whole_ban", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_leave(int64_t group_id, int is_dismiss) {
    json p; p["group_id"] = group_id; p["is_dismiss"] = is_dismiss != 0;
    return plugin_call_bool("set_group_leave", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_card(int64_t group_id, int64_t user_id, const char* card) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["card"] = card ? card : "";
    return plugin_call_bool("set_group_card", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_special_title(int64_t group_id, int64_t user_id, const char* special_title) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id; p["special_title"] = special_title ? special_title : "";
    return plugin_call_bool("set_group_special_title", p);
}

static int YUEX_PLUGIN_CALL plugin_set_friend_add_request(const char* flag, int approve, const char* remark) {
    json p; p["flag"] = flag ? flag : ""; p["approve"] = approve != 0;
    if (remark && *remark) p["remark"] = remark;
    return plugin_call_bool("set_friend_add_request", p);
}

static int YUEX_PLUGIN_CALL plugin_set_group_add_request(const char* flag, const char* sub_type, int approve, const char* reason) {
    json p; p["flag"] = flag ? flag : ""; p["sub_type"] = sub_type ? sub_type : ""; p["approve"] = approve != 0;
    if (reason && *reason) p["reason"] = reason;
    return plugin_call_bool("set_group_add_request", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_info(int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge("get_group_info", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_member_info(int64_t group_id, int64_t user_id) {
    json p; p["group_id"] = group_id; p["user_id"] = user_id;
    return plugin_store_json(call_onebot_api_bridge("get_group_member_info", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_root_files(int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge("get_group_root_files", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_files(int64_t group_id, const char* folder_id, int32_t start_index) {
    json p; p["group_id"] = group_id; p["start_index"] = start_index;
    if (folder_id && *folder_id) p["folder_id"] = folder_id;
    return plugin_store_json(call_onebot_api_bridge("get_group_files", p));
}

static int YUEX_PLUGIN_CALL plugin_upload_group_file(int64_t group_id, const char* file, const char* name) {
    json p; p["group_id"] = group_id; p["file"] = file ? file : "";
    if (name && *name) p["name"] = name;
    return plugin_call_bool("upload_group_file", p);
}

static int YUEX_PLUGIN_CALL plugin_delete_group_file(int64_t group_id, const char* file_id, int32_t busid) {
    json p; p["group_id"] = group_id; p["file_id"] = file_id ? file_id : ""; p["busid"] = busid;
    return plugin_call_bool("delete_group_file", p);
}

static int YUEX_PLUGIN_CALL plugin_move_group_file(int64_t group_id, const char* file_id, const char* parent_folder, const char* target_folder) {
    json p; p["group_id"] = group_id; p["file_id"] = file_id ? file_id : "";
    p["parent_folder"] = parent_folder ? parent_folder : "";
    p["target_folder"] = target_folder ? target_folder : "";
    return plugin_call_bool("move_group_file", p);
}

static int YUEX_PLUGIN_CALL plugin_create_group_folder(int64_t group_id, const char* folder_name) {
    json p; p["group_id"] = group_id; p["folder_name"] = folder_name ? folder_name : "";
    return plugin_call_bool("create_group_folder", p);
}

static int YUEX_PLUGIN_CALL plugin_delete_group_folder(int64_t group_id, const char* folder_id) {
    json p; p["group_id"] = group_id; p["folder_id"] = folder_id ? folder_id : "";
    return plugin_call_bool("delete_group_folder", p);
}

static int YUEX_PLUGIN_CALL plugin_rename_group_folder(int64_t group_id, const char* folder_id, const char* new_folder_name) {
    json p; p["group_id"] = group_id; p["folder_id"] = folder_id ? folder_id : ""; p["new_folder_name"] = new_folder_name ? new_folder_name : "";
    return plugin_call_bool("rename_group_folder", p);
}

static int YUEX_PLUGIN_CALL plugin_set_avatar(const char* file) {
    json p; p["file"] = file ? file : "";
    return plugin_call_bool("set_avatar", p);
}

static int YUEX_PLUGIN_CALL plugin_set_nickname(const char* nickname) {
    json p; p["nickname"] = nickname ? nickname : "";
    return plugin_call_bool("set_nickname", p);
}

static int YUEX_PLUGIN_CALL plugin_set_bio(const char* bio) {
    json p; p["bio"] = bio ? bio : "";
    return plugin_call_bool("set_bio", p);
}

static int YUEX_PLUGIN_CALL plugin_send_like(int64_t user_id, int32_t times) {
    json p; p["user_id"] = user_id; p["times"] = times;
    return plugin_call_bool("send_like", p);
}

static int YUEX_PLUGIN_CALL plugin_send_group_notice(int64_t group_id, const char* content, const char* image) {
    json p; p["group_id"] = group_id; p["content"] = content ? content : "";
    if (image && *image) p["image"] = image;
    return plugin_call_bool("_send_group_notice", p);
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_notice(int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge("_get_group_notice", p));
}

static const char* YUEX_PLUGIN_CALL plugin_get_custom_face_url_list() {
    return plugin_store_json(call_onebot_api_bridge("get_custom_face_url_list"));
}

static const char* YUEX_PLUGIN_CALL plugin_get_group_essence_msg_list(int64_t group_id) {
    json p; p["group_id"] = group_id;
    return plugin_store_json(call_onebot_api_bridge("get_group_essence_msg_list", p));
}

static std::string sanitize_plugin_id(const char* plugin_id) {
    std::string id = plugin_id && *plugin_id ? plugin_id : "default";
    for (char& c : id) {
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) c = '_';
    }
    return id;
}

static json load_plugin_config_file(const std::string& pluginId) {
    json cfg = load_config_file("plugins/" + pluginId + ".json");
    if (!cfg.is_null()) return cfg;

    std::string manifestPath = path_join(g_pluginDir, pluginId + ".json");
    FILE* f = fopen(manifestPath.c_str(), "r");
    if (!f) return json();
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string buf(sz > 0 ? (size_t)sz : 0, '\0');
    if (!buf.empty()) fread(&buf[0], 1, buf.size(), f);
    fclose(f);
    try {
        cfg = json::parse(buf);
        if (cfg.is_object()) save_config_file("plugins/" + pluginId + ".json", cfg);
        return cfg;
    } catch (...) {
        return json();
    }
}

static void save_plugin_config_file(const std::string& pluginId, const json& data) {
    ensure_directory_tree(path_join(g_configDir, "plugins"));
    save_config_file("plugins/" + pluginId + ".json", data);
}

static const char* YUEX_PLUGIN_CALL plugin_get_plugin_config(const char* plugin_id, const char* key, const char* default_value) {
    std::string id = sanitize_plugin_id(plugin_id);
    if (!plugin_context_can_access_plugin_config(id, "config")) {
        std::lock_guard<std::mutex> lock(g_pluginApiMutex);
        g_pluginApiScratch = default_value ? default_value : "";
        g_pluginApiLastError = "plugin permission denied: config";
        return g_pluginApiScratch.c_str();
    }
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    std::string k = key ? key : "";
    auto cfg = load_plugin_config_file(id);
    if (cfg.is_object() && cfg.contains(k)) {
        if (cfg[k].is_string()) g_pluginApiScratch = cfg[k].get<std::string>();
        else g_pluginApiScratch = cfg[k].dump();
    } else {
        g_pluginApiScratch = default_value ? default_value : "";
    }
    return g_pluginApiScratch.c_str();
}

static int YUEX_PLUGIN_CALL plugin_set_plugin_config(const char* plugin_id, const char* key, const char* value) {
    std::string id = sanitize_plugin_id(plugin_id);
    if (!plugin_context_can_access_plugin_config(id, "config")) {
        std::lock_guard<std::mutex> lock(g_pluginApiMutex);
        g_pluginApiLastError = "plugin permission denied: config";
        return 0;
    }
    std::string k = key ? key : "";
    if (k.empty()) return 0;
    auto cfg = load_plugin_config_file(id);
    if (!cfg.is_object()) cfg = json::object();
    cfg[k] = value ? value : "";
    save_plugin_config_file(id, cfg);
    return 1;
}

static const char* YUEX_PLUGIN_CALL plugin_get_data_dir(const char* plugin_id) {
    std::string id = sanitize_plugin_id(plugin_id);
    if (!plugin_context_can_access_plugin_config(id, "data_dir")) {
        std::lock_guard<std::mutex> lock(g_pluginApiMutex);
        g_pluginApiScratch.clear();
        g_pluginApiLastError = "plugin permission denied: data_dir";
        return g_pluginApiScratch.c_str();
    }
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    g_pluginApiScratch = path_join(path_join(g_configDir, "plugins"), id);
    ensure_directory_tree(g_pluginApiScratch);
    return g_pluginApiScratch.c_str();
}

static void YUEX_PLUGIN_CALL plugin_log(const char* level, const char* message) {
    std::string lv = level && *level ? level : "info";
    add_log("system", "Plugin/" + lv, message ? message : "");
}

static const char* YUEX_PLUGIN_CALL plugin_get_last_result() {
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    return g_pluginApiLastResult.c_str();
}

static const char* YUEX_PLUGIN_CALL plugin_get_last_error() {
    std::lock_guard<std::mutex> lock(g_pluginApiMutex);
    return g_pluginApiLastError.c_str();
}

static const char* YUEX_PLUGIN_CALL plugin_get_plugin_permissions(const char* plugin_id) {
    std::string id = sanitize_plugin_id(plugin_id);
    json cfg = load_plugin_config_file(id);
    bool settingsAvailable = false;
    std::vector<std::string> permissions;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_plugins) {
            if (p.id == id) {
                settingsAvailable = p.openSettings != nullptr;
                permissions = p.permissions;
                break;
            }
        }
    }
    if (permissions.empty()) permissions = plugin_permissions_vector_from_json(cfg, settingsAvailable);
    return plugin_store_json(plugin_permissions_json_from_vector(permissions));
}

static int YUEX_PLUGIN_CALL plugin_has_plugin_permission(const char* plugin_id, const char* permission) {
    std::string id = sanitize_plugin_id(plugin_id);
    std::string want = permission ? permission : "";
    if (want.empty()) return 0;
    std::vector<std::string> perms;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_plugins) {
            if (p.id == id) {
                perms = p.permissions;
                break;
            }
        }
    }
    if (perms.empty()) {
        perms = plugin_permissions_vector_from_json(load_plugin_config_file(id), false);
    }
    return std::find(perms.begin(), perms.end(), want) != perms.end() ? 1 : 0;
}

static uint32_t default_plugin_event_mask() {
    return (1u << YUEX_EVENT_MESSAGE) | (1u << YUEX_EVENT_NOTICE) |
           (1u << YUEX_EVENT_REQUEST) | (1u << YUEX_EVENT_META);
}

static int YUEX_PLUGIN_CALL plugin_set_event_filter(const char* plugin_id, uint32_t event_mask) {
    std::string id = sanitize_plugin_id(plugin_id);
    if (id.empty()) return 0;
    (void)event_mask;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_plugins) {
            if (p.id == id) {
                p.eventMask = default_plugin_event_mask();
                break;
            }
        }
    }
    return 1;
}

static uint32_t YUEX_PLUGIN_CALL plugin_get_event_filter(const char* plugin_id) {
    std::string id = sanitize_plugin_id(plugin_id);
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_plugins) {
            if (p.id == id) return p.eventMask;
        }
    }
    return default_plugin_event_mask();
}

static void init_plugin_api() {
    g_pluginApi.abi_version = YUEX_PLUGIN_ABI_VERSION;
    g_pluginApi.call_onebot_api = plugin_call_onebot_api;
    g_pluginApi.get_active_account = plugin_get_active_account;
    g_pluginApi.get_accounts = plugin_get_accounts;
    g_pluginApi.get_login_info = plugin_get_login_info;
    g_pluginApi.get_status = plugin_get_status;
    g_pluginApi.get_friend_list = plugin_get_friend_list;
    g_pluginApi.delete_friend = plugin_delete_friend;
    g_pluginApi.get_group_list = plugin_get_group_list;
    g_pluginApi.get_group_member_list = plugin_get_group_member_list;
    g_pluginApi.get_group_info = plugin_get_group_info;
    g_pluginApi.get_group_member_info = plugin_get_group_member_info;
    g_pluginApi.send_msg = plugin_send_msg;
    g_pluginApi.send_private_msg = plugin_send_private_msg;
    g_pluginApi.send_group_msg = plugin_send_group_msg;
    g_pluginApi.delete_msg = plugin_delete_msg;
    g_pluginApi.get_msg = plugin_get_msg;
    g_pluginApi.get_forward_msg = plugin_get_forward_msg;
    g_pluginApi.set_group_ban = plugin_set_group_ban;
    g_pluginApi.set_group_kick = plugin_set_group_kick;
    g_pluginApi.set_group_admin = plugin_set_group_admin;
    g_pluginApi.set_group_name = plugin_set_group_name;
    g_pluginApi.set_group_whole_ban = plugin_set_group_whole_ban;
    g_pluginApi.set_group_leave = plugin_set_group_leave;
    g_pluginApi.set_group_card = plugin_set_group_card;
    g_pluginApi.set_group_special_title = plugin_set_group_special_title;
    g_pluginApi.set_friend_add_request = plugin_set_friend_add_request;
    g_pluginApi.set_group_add_request = plugin_set_group_add_request;
    g_pluginApi.get_group_root_files = plugin_get_group_root_files;
    g_pluginApi.get_group_files = plugin_get_group_files;
    g_pluginApi.upload_group_file = plugin_upload_group_file;
    g_pluginApi.delete_group_file = plugin_delete_group_file;
    g_pluginApi.move_group_file = plugin_move_group_file;
    g_pluginApi.create_group_folder = plugin_create_group_folder;
    g_pluginApi.delete_group_folder = plugin_delete_group_folder;
    g_pluginApi.rename_group_folder = plugin_rename_group_folder;
    g_pluginApi.set_avatar = plugin_set_avatar;
    g_pluginApi.set_nickname = plugin_set_nickname;
    g_pluginApi.set_bio = plugin_set_bio;
    g_pluginApi.send_like = plugin_send_like;
    g_pluginApi.send_group_notice = plugin_send_group_notice;
    g_pluginApi.get_group_notice = plugin_get_group_notice;
    g_pluginApi.get_custom_face_url_list = plugin_get_custom_face_url_list;
    g_pluginApi.get_group_essence_msg_list = plugin_get_group_essence_msg_list;
    g_pluginApi.get_plugin_config = plugin_get_plugin_config;
    g_pluginApi.set_plugin_config = plugin_set_plugin_config;
    g_pluginApi.get_data_dir = plugin_get_data_dir;
    g_pluginApi.log = plugin_log;
    g_pluginApi.get_sdk_info = plugin_get_sdk_info;
    g_pluginApi.get_framework_version = plugin_get_framework_version;
    g_pluginApi.get_account_status = plugin_get_account_status;
    g_pluginApi.call_onebot_api_as = plugin_call_onebot_api_as;
    g_pluginApi.call_onebot_api_ex = plugin_call_onebot_api_ex;
    g_pluginApi.call_onebot_api_as_ex = plugin_call_onebot_api_as_ex;
    g_pluginApi.get_friend_list_as = plugin_get_friend_list_as;
    g_pluginApi.get_group_list_as = plugin_get_group_list_as;
    g_pluginApi.get_avatar_url = plugin_get_avatar_url;
    g_pluginApi.get_login_info_as = plugin_get_login_info_as;
    g_pluginApi.get_status_as = plugin_get_status_as;
    g_pluginApi.get_group_member_list_as = plugin_get_group_member_list_as;
    g_pluginApi.send_msg_as = plugin_send_msg_as;
    g_pluginApi.send_private_msg_as = plugin_send_private_msg_as;
    g_pluginApi.send_group_msg_as = plugin_send_group_msg_as;
    g_pluginApi.delete_msg_as = plugin_delete_msg_as;
    g_pluginApi.get_msg_as = plugin_get_msg_as;
    g_pluginApi.get_forward_msg_as = plugin_get_forward_msg_as;
    g_pluginApi.get_group_info_as = plugin_get_group_info_as;
    g_pluginApi.get_group_member_info_as = plugin_get_group_member_info_as;
    g_pluginApi.set_group_ban_as = plugin_set_group_ban_as;
    g_pluginApi.set_group_kick_as = plugin_set_group_kick_as;
    g_pluginApi.set_group_admin_as = plugin_set_group_admin_as;
    g_pluginApi.set_group_name_as = plugin_set_group_name_as;
    g_pluginApi.set_group_whole_ban_as = plugin_set_group_whole_ban_as;
    g_pluginApi.set_group_card_as = plugin_set_group_card_as;
    g_pluginApi.set_group_leave_as = plugin_set_group_leave_as;
    g_pluginApi.set_group_special_title_as = plugin_set_group_special_title_as;
    g_pluginApi.set_friend_add_request_as = plugin_set_friend_add_request_as;
    g_pluginApi.set_group_add_request_as = plugin_set_group_add_request_as;
    g_pluginApi.get_group_root_files_as = plugin_get_group_root_files_as;
    g_pluginApi.get_group_files_as = plugin_get_group_files_as;
    g_pluginApi.upload_group_file_as = plugin_upload_group_file_as;
    g_pluginApi.delete_group_file_as = plugin_delete_group_file_as;
    g_pluginApi.move_group_file_as = plugin_move_group_file_as;
    g_pluginApi.create_group_folder_as = plugin_create_group_folder_as;
    g_pluginApi.delete_group_folder_as = plugin_delete_group_folder_as;
    g_pluginApi.rename_group_folder_as = plugin_rename_group_folder_as;
    g_pluginApi.send_like_as = plugin_send_like_as;
    g_pluginApi.send_group_notice_as = plugin_send_group_notice_as;
    g_pluginApi.get_group_notice_as = plugin_get_group_notice_as;
    g_pluginApi.get_custom_face_url_list_as = plugin_get_custom_face_url_list_as;
    g_pluginApi.get_group_essence_msg_list_as = plugin_get_group_essence_msg_list_as;
    g_pluginApi.get_version_info_as = plugin_get_version_info_as;
    g_pluginApi.get_stranger_info_as = plugin_get_stranger_info_as;
    g_pluginApi.get_group_honor_info_as = plugin_get_group_honor_info_as;
    g_pluginApi.get_record_as = plugin_get_record_as;
    g_pluginApi.get_image_as = plugin_get_image_as;
    g_pluginApi.can_send_image_as = plugin_can_send_image_as;
    g_pluginApi.can_send_record_as = plugin_can_send_record_as;
    g_pluginApi.get_cookies_as = plugin_get_cookies_as;
    g_pluginApi.get_csrf_token_as = plugin_get_csrf_token_as;
    g_pluginApi.get_credentials_as = plugin_get_credentials_as;
    g_pluginApi.get_group_shut_list_as = plugin_get_group_shut_list_as;
    g_pluginApi.get_group_at_all_remain_as = plugin_get_group_at_all_remain_as;
    g_pluginApi.set_group_portrait_as = plugin_set_group_portrait_as;
    g_pluginApi.upload_private_file_as = plugin_upload_private_file_as;
    g_pluginApi.fetch_custom_face_as = plugin_fetch_custom_face_as;
    g_pluginApi.get_last_result = plugin_get_last_result;
    g_pluginApi.get_last_error = plugin_get_last_error;
    g_pluginApi.get_plugin_permissions = plugin_get_plugin_permissions;
    g_pluginApi.has_plugin_permission = plugin_has_plugin_permission;
    g_pluginApi.set_event_filter = plugin_set_event_filter;
    g_pluginApi.get_event_filter = plugin_get_event_filter;
    g_pluginApi.ocr_image_as = plugin_ocr_image_as;
    g_pluginApi.get_rkey_as = plugin_get_rkey_as;
    g_pluginApi.get_clientkey_as = plugin_get_clientkey_as;
    g_pluginApi.get_group_album_list_as = plugin_get_group_album_list_as;
    g_pluginApi.get_group_album_media_list_as = plugin_get_group_album_media_list_as;
    g_pluginApi.get_group_system_msg_as = plugin_get_group_system_msg_as;
    g_pluginApi.get_group_ignore_add_request_as = plugin_get_group_ignore_add_request_as;
    g_pluginApi.get_group_file_url_as = plugin_get_group_file_url_as;
    g_pluginApi.download_file_as = plugin_download_file_as;
    g_pluginApi.get_ai_characters_as = plugin_get_ai_characters_as;
    g_pluginApi.send_group_ai_record_as = plugin_send_group_ai_record_as;
}

static int event_type_to_plugin(const std::string& postType) {
    if (postType == "message") return YUEX_EVENT_MESSAGE;
    if (postType == "notice") return YUEX_EVENT_NOTICE;
    if (postType == "request") return YUEX_EVENT_REQUEST;
    if (postType == "meta_event") return YUEX_EVENT_META;
    return 0;
}

static void unload_plugin(PluginRuntime& p) {
    std::lock_guard<std::mutex> dispatchLock(g_pluginDispatchMutex);
    if (p.enabled && p.shutdown) {
        try {
            g_pluginCallContextId = p.id;
            p.shutdown();
        } catch (...) {}
        g_pluginCallContextId.clear();
    }
    p.enabled = false;
    if (p.module) {
        FreeLibrary(p.module);
        p.module = nullptr;
    }
    p.init = nullptr;
    p.shutdown = nullptr;
    p.onEvent = nullptr;
    p.openSettings = nullptr;
}

template<typename T>
static T get_proc_any(HMODULE mod, std::initializer_list<const char*> names) {
    for (auto name : names) {
        auto fn = (T)GetProcAddress(mod, name);
        if (fn) return fn;
    }
    return nullptr;
}

static std::string plugin_stem_from_path(const std::string& path) {
    std::string name = path;
    size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name.resize(dot);
    return normalize_external_text(name);
}

static std::string xlz_host32_path() {
    std::string preferred = path_join(g_binDir, "YuexPluginHost32.exe");
    if (file_exists(preferred)) return preferred;
    return path_join(g_rootDir, "YuexPluginHost32.exe");
}

static bool xlz_host32_available() {
    return file_exists(xlz_host32_path());
}

static std::string quote_cmd_arg(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '\"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('\"');
    return out;
}

static std::wstring quote_cmd_arg_w(const std::wstring& s) {
    std::wstring out = L"\"";
    for (wchar_t c : s) {
        if (c == L'\"' || c == L'\\') out.push_back(L'\\');
        out.push_back(c);
    }
    out.push_back(L'\"');
    return out;
}

static std::shared_ptr<XlzBridgeRuntime> find_xlz_bridge_locked(const std::string& id) {
    for (auto& p : g_xlzBridgePlugins) if (p && p->id == id) return p;
    return nullptr;
}

static void add_reserved_plugin(const std::string& path, const std::string& type, const std::string& reason) {
    ReservedPluginRuntime p;
    p.id = plugin_stem_from_path(path);
    p.name = p.id;
    if (type == "xiaolizi-x86") {
        p.name = p.id + " (x86)";
        p.description = "小栗子 XLZ x86/易语言插件，需要 YuexPluginHost32.exe 宿主桥接后运行";
    } else {
        p.description = reason;
    }
    p.path = normalize_external_text(path);
    p.type = type;
    p.reason = reason;
    std::lock_guard<std::mutex> lock(g_pluginMutex);
    g_reservedPlugins.push_back(p);
}

static bool load_plugin_dll(const std::string& path, bool enable) {
    std::string normalizedPath = normalize_external_text(path);
    std::wstring wpath = utf8_to_wide(path);
    HMODULE mod = !wpath.empty() ? LoadLibraryW(wpath.c_str()) : LoadLibraryA(path.c_str());
    if (!mod) {
        DWORD err = GetLastError();
        std::string reason = plugin_load_error_reason(err);
        if (err == ERROR_BAD_EXE_FORMAT) {
            std::string reserveReason = "检测到小栗子 XLZ x86/易语言 DLL，x64 主程序不能直接载入；需要 YuexPluginHost32.exe 32 位宿主桥接。";
            add_reserved_plugin(normalizedPath, "xiaolizi-x86", reserveReason + " " + reason);
            add_log("warning", "", "已识别 32 位插件: " + normalizedPath + "；" + reserveReason);
            startup_trace("plugin reserved x86 path=%s err=%lu reason=%s", normalizedPath.c_str(), (unsigned long)err, reason.c_str());
        } else {
            add_reserved_plugin(normalizedPath, "load-error", reason);
            add_log("error", "", "插件 DLL 加载失败: " + normalizedPath + "；原因: " + reason);
            startup_trace("plugin load failed path=%s err=%lu reason=%s", normalizedPath.c_str(), (unsigned long)err, reason.c_str());
        }
        return false;
    }
    auto getInfo = get_proc_any<YuexPluginGetInfoFn>(mod, {"yuex_plugin_get_info", "plugin_get_info", "XiaoLiZiPluginGetInfo"});
    auto init = get_proc_any<YuexPluginInitFn>(mod, {"yuex_plugin_init", "plugin_init", "XiaoLiZiPluginInit"});
    auto shutdown = get_proc_any<YuexPluginShutdownFn>(mod, {"yuex_plugin_shutdown", "plugin_shutdown", "XiaoLiZiPluginShutdown"});
    auto onEvent = get_proc_any<YuexPluginOnEventFn>(mod, {"yuex_plugin_on_event", "plugin_on_event", "XiaoLiZiPluginOnEvent"});
    auto openSettings = get_proc_any<int (YUEX_PLUGIN_CALL *)()>(mod, {"yuex_plugin_open_settings", "plugin_open_settings", "XiaoLiZiPluginOpenSettings"});
    if (!getInfo || !init || !shutdown || !onEvent) {
        auto apprun = GetProcAddress(mod, "apprun");
        FreeLibrary(mod);
        if (apprun) {
            add_log("system", "", "已忽略小栗子插件: " + path);
            startup_trace("xiaolizi plugin ignored: %s", path.c_str());
        } else {
            add_log("system", "", "已忽略非 YuexBot 插件: " + path);
            startup_trace("plugin ignored missing yuex exports path=%s", path.c_str());
        }
        return true;
    }
    YuexPluginInfo info = {};
    if (!getInfo(&info) || info.abi_version != YUEX_PLUGIN_ABI_VERSION) {
        uint32_t gotAbi = info.abi_version;
        FreeLibrary(mod);
        std::string reason = "ABI 不兼容，当前框架 ABI=" + std::to_string(YUEX_PLUGIN_ABI_VERSION) + "，插件 ABI=" + std::to_string(gotAbi);
        add_reserved_plugin(path, "abi-mismatch", reason);
        add_log("error", "", "插件 ABI 版本不兼容: " + path + "；" + reason);
        startup_trace("plugin abi mismatch path=%s framework=%u plugin=%u", path.c_str(), YUEX_PLUGIN_ABI_VERSION, gotAbi);
        return false;
    }
    PluginRuntime p;
    p.id = normalize_external_text(info.id ? info.id : path);
    p.name = normalize_external_text(info.name ? info.name : p.id);
    p.version = normalize_external_text(info.version ? info.version : "");
    p.author = normalize_external_text(info.author ? info.author : "");
    p.description = normalize_external_text(info.description ? info.description : "");
    p.path = normalize_external_text(path);
    p.module = mod;
    p.init = init;
    p.shutdown = shutdown;
    p.onEvent = onEvent;
    p.openSettings = openSettings;
    {
        json cfg = load_plugin_config_file(sanitize_plugin_id(p.id.c_str()));
        p.permissions = plugin_permissions_vector_from_json(cfg, p.openSettings != nullptr);
        p.eventMask = default_plugin_event_mask();
    }
    p.enabled = false;
    if (enable) {
        try {
            g_pluginCallContextId = p.id;
            p.enabled = p.init(&g_pluginApi) != 0;
        } catch (...) { p.enabled = false; }
        g_pluginCallContextId.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        g_plugins.push_back(p);
    }
    add_log("system", "", "已载入 YuexBot 插件: " + p.name);
    startup_trace("loaded YuexBot plugin: %s", p.name.c_str());
    return true;
}

static PluginRuntime* find_plugin_locked(const std::string& id) {
    for (auto& p : g_plugins) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

static bool get_reserved_plugin_copy(const std::string& id, ReservedPluginRuntime& out) {
    std::string normalizedId = normalize_external_text(id);
    std::lock_guard<std::mutex> lock(g_pluginMutex);
    for (const auto& p : g_reservedPlugins) {
        if (p.id == id || normalize_external_text(p.id) == normalizedId) {
            out = p;
            out.id = normalize_external_text(out.id);
            out.name = normalize_external_text(out.name);
            return true;
        }
    }
    return false;
}

static std::string reserved_plugin_error_message(const ReservedPluginRuntime& p, const std::string& action) {
    if (p.type == "xiaolizi-x86") {
        if (!xlz_host32_available()) return "小栗子 XLZ x86/易语言插件已识别，但 " + action + " 需要 YuexPluginHost32.exe 32 位宿主桥接；当前未在 main/bin 找到宿主。";
        return "小栗子 XLZ x86/易语言插件桥接启动失败，请查看插件详情里的失败原因和 Host32 状态。";
    }
    if (!p.reason.empty()) return p.reason;
    return "该插件当前不能由 YuexBot 直接载入";
}

static json plugin_settings() {
    auto s = load_config_file("settings.json");
    if (!s.contains("plugins") || !s["plugins"].is_object()) s["plugins"] = json::object();
    return s;
}

static bool start_xlz_bridge_plugin(const ReservedPluginRuntime& reserved, std::string& error);

static bool plugin_enabled_from_settings(const json& settings, const std::string& id, bool fallback = false) {
    if (!settings.contains("plugins") || !settings["plugins"].is_object()) return fallback;
    const auto& plugins = settings["plugins"];
    if (plugins.contains(id) && plugins[id].is_boolean()) return plugins[id].get<bool>();
    std::string normalizedId = normalize_external_text(id);
    for (auto it = plugins.begin(); it != plugins.end(); ++it) {
        if (normalize_external_text(it.key()) == normalizedId && it.value().is_boolean()) return it.value().get<bool>();
    }
    return fallback;
}

static void save_plugin_enabled(const std::string& id, bool enabled) {
    std::string normalizedId = normalize_external_text(id);
    auto s = load_config_file("settings.json");
    if (!s.is_object()) s = json::object();
    if (!s.contains("plugins") || !s["plugins"].is_object()) s["plugins"] = json::object();
    if (s["plugins"].is_object()) {
        std::vector<std::string> removeKeys;
        for (auto it = s["plugins"].begin(); it != s["plugins"].end(); ++it) {
            if (normalize_external_text(it.key()) == normalizedId && it.key() != normalizedId) removeKeys.push_back(it.key());
        }
        for (const auto& key : removeKeys) s["plugins"].erase(key);
    }
    s["plugins"][normalizedId] = enabled;
    save_config_file("settings.json", s);
}

static void scan_plugins() {
    std::vector<PluginRuntime> oldPlugins;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        oldPlugins.swap(g_plugins);
        g_reservedPlugins.clear();
    }
    for (auto& p : oldPlugins) unload_plugin(p);
    xlz_unload_all();
    ensure_directory_tree(g_pluginDir);
    json settings = plugin_settings();
      WIN32_FIND_DATAW fd;
      std::string pattern = path_join(g_pluginDir, "*.dll");
      std::wstring wpattern = utf8_to_wide(pattern);
      HANDLE h = FindFirstFileW(wpattern.c_str(), &fd);
      if (h == INVALID_HANDLE_VALUE) return;
      std::vector<ReservedPluginRuntime> bridgesToStart;
      do {
          if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
          std::string fileName = wide_to_utf8(fd.cFileName);
          std::string path = path_join(g_pluginDir, fileName);
          std::string idGuess = plugin_stem_from_path(fileName);
          bool enabled = plugin_enabled_from_settings(settings, idGuess, false);
          load_plugin_dll(path, enabled);
          ReservedPluginRuntime reserved;
          if (get_reserved_plugin_copy(idGuess, reserved) && reserved.type == "xiaolizi-x86" && enabled) {
              bridgesToStart.push_back(reserved);
          }
      } while (FindNextFileW(h, &fd));
      FindClose(h);
      for (auto& reserved : bridgesToStart) {
          std::string err;
          if (!start_xlz_bridge_plugin(reserved, err)) add_log("error", reserved.name, "小栗子 x86 桥接自动启动失败: " + err);
      }
      trim_working_set_soft();
}

static json plugins_to_json() {
    json arr = json::array();
    json enabled = json::object();
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_plugins) {
            json j;
            json permissions = plugin_permissions_json_from_vector(p.permissions);
            j["id"] = p.id;
            j["name"] = p.name;
            j["version"] = p.version;
            j["author"] = p.author;
            j["description"] = p.description;
            j["path"] = p.path;
            j["enabled"] = p.enabled;
            j["type"] = "yuexbot";
            j["abi_version"] = YUEX_PLUGIN_ABI_VERSION;
            j["reserved"] = false;
            j["settings_available"] = p.openSettings != nullptr;
            j["permissions"] = permissions;
            json metrics;
            metrics["events"] = p.eventCount;
            metrics["errors"] = p.eventErrorCount;
            metrics["slow_events"] = p.slowEventCount;
            metrics["avg_ms"] = p.eventCount ? (double)p.totalEventMs / (double)p.eventCount : 0.0;
            metrics["max_ms"] = p.maxEventMs;
            metrics["last_event_at"] = p.lastEventAt;
            metrics["last_error_at"] = p.lastErrorAt;
            metrics["last_result"] = p.lastEventResult;
            metrics["last_error"] = p.lastError;
            j["metrics"] = metrics;
            arr.push_back(j);
            enabled[p.id] = p.enabled;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
        for (auto& p : g_xlzPlugins) {
            json j;
            j["id"] = p.id;
            j["name"] = p.name;
            j["version"] = p.version;
            j["author"] = p.author;
            j["description"] = p.description;
            j["path"] = p.path;
            j["enabled"] = p.enabled;
            j["type"] = "xiaolizi";
            j["reserved"] = false;
            j["settings_available"] = p.enabled && p.fnAppSetting != nullptr;
            arr.push_back(j);
            enabled[p.id] = p.enabled;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_xlzBridgePlugins) {
            if (!p) continue;
            json j;
            j["id"] = p->id;
            j["name"] = p->name.empty() ? p->id + " (x86)" : p->name;
            j["sdk_version"] = p->sdkVersion;
            j["version"] = p->version;
            j["author"] = p->author;
            j["description"] = p->description.empty() ? "小栗子 XLZ x86/易语言插件，正在通过 YuexPluginHost32.exe 桥接运行" : p->description;
            j["path"] = p->path;
            j["enabled"] = p->enabled;
            j["type"] = "xiaolizi-x86";
            j["reserved"] = false;
            j["bridge"] = true;
            j["process_id"] = p->processId;
              j["host_required"] = true;
              j["host_name"] = "YuexPluginHost32.exe";
              j["host_available"] = xlz_host32_available();
              j["host_path"] = xlz_host32_path();
            j["settings_available"] = p->enabled && p->hasSettings;
            j["has_group_callback"] = p->hasGroupCallback;
            j["has_private_callback"] = p->hasPrivateCallback;
            j["has_event_callback"] = p->hasEventCallback;
            j["has_enable_callback"] = p->hasEnableCallback;
              json metrics;
            metrics["events"] = p->dispatchCount.load();
            metrics["dispatch_ok"] = p->dispatchOkCount.load();
            metrics["api_calls"] = p->apiCount.load();
            metrics["errors"] = p->errorCount.load();
            metrics["last_event_at"] = p->lastEventAt.load();
            metrics["last_api_at"] = p->lastApiAt.load();
            metrics["last_host_at"] = p->lastHostAt.load();
            j["plugin_kind"] = p->pluginKind;
            j["enable_ok"] = p->enableOk;
            j["reader_running"] = p->readerRunning.load();
            j["last_host_event"] = p->lastHostEvent;
            j["last_event_type"] = p->lastEventType;
            j["last_message_type"] = p->lastMessageType;
            j["last_notice_type"] = p->lastNoticeType;
            j["last_api_action"] = p->lastApiAction;
            if (!p->lastError.empty()) j["reason"] = p->lastError;
            j["metrics"] = metrics;
            arr.push_back(j);
            enabled[p->id] = p->enabled;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_reservedPlugins) {
            if (p.type == "xiaolizi-x86" && find_xlz_bridge_locked(p.id)) continue;
            json j;
            j["id"] = p.id;
            j["name"] = p.name;
            j["version"] = "";
            j["author"] = "";
            j["description"] = p.description;
            j["path"] = p.path;
            j["enabled"] = false;
            j["type"] = p.type;
            j["reserved"] = true;
            j["reason"] = p.reason;
            j["host_required"] = p.type == "xiaolizi-x86";
            j["host_name"] = p.type == "xiaolizi-x86" ? "YuexPluginHost32.exe" : "";
            j["host_available"] = p.type == "xiaolizi-x86" ? xlz_host32_available() : false;
            j["host_path"] = p.type == "xiaolizi-x86" ? xlz_host32_path() : "";
            j["settings_available"] = false;
            if (p.type == "xiaolizi-x86") {
                j["bridge"] = true;
                j["plugin_kind"] = "pending-x86";
                j["enable_ok"] = false;
                j["metrics"] = json{{"events", 0}, {"dispatch_ok", 0}, {"api_calls", 0}, {"errors", 0}};
            }
            arr.push_back(j);
        }
    }
    json r;
    r["plugins"] = arr;
    r["enabledPlugins"] = enabled;
    return r;
}

static std::string json_first_string(const json& j, std::initializer_list<const char*> keys) {
    for (auto key : keys) {
        if (j.contains(key) && j[key].is_string()) {
            std::string v = normalize_external_text(j[key].get<std::string>());
            if (!v.empty()) return v;
        }
    }
    return "";
}

static void parse_xlz_bridge_appinfo(const std::string& appInfoRaw, XlzBridgeRuntime& p) {
    if (appInfoRaw.empty()) return;
    try {
        json app = json::parse(appInfoRaw);
        std::string name = json_first_string(app, {"name", "appname", "app_name", "插件名称"});
        std::string version = json_first_string(app, {"version", "appv", "appver", "app_version", "插件版本"});
        std::string author = json_first_string(app, {"author", "auth", "作者"});
        std::string description = json_first_string(app, {"description", "describe", "desc", "说明"});
        std::string sdkVersion = json_first_string(app, {"sdkv", "sdk_version", "sdkVersion"});
        if (!name.empty()) p.name = name;
        if (!version.empty()) p.version = version;
        if (!author.empty()) p.author = author;
        if (!description.empty()) p.description = description;
        if (!sdkVersion.empty()) p.sdkVersion = sdkVersion;
    } catch (...) {}
}

static bool write_xlz_bridge_command(const std::shared_ptr<XlzBridgeRuntime>& rt, const json& cmd) {
    if (!rt || !rt->stdinWrite) return false;
    std::string line = cmd.dump() + "\n";
    DWORD written = 0;
    return WriteFile(rt->stdinWrite, line.data(), (DWORD)line.size(), &written, NULL) && written == line.size();
}

static std::string xlz_bridge_account_ref_from_msg(const json& msg) {
    std::string selfId = json_value_string(msg, "self_id");
    if (!selfId.empty() && selfId != "0") return selfId;
    return g_activeAccountId;
}

static json call_xlz_bridge_onebot_request(const json& msg) {
    try {
        std::string accountRef = xlz_bridge_account_ref_from_msg(msg);
        std::string event = msg.value("event", "");
        if (event == "send_group_msg") {
            json p;
            p["group_id"] = json_value_i64(msg, "group_id");
            p["message"] = normalize_external_text(msg.value("message", ""));
            return accountRef.empty() ? call_onebot_api_bridge("send_group_msg", p) : call_onebot_api_bridge_for_account(accountRef, "send_group_msg", p);
        }
        if (event == "send_private_msg") {
            json p;
            p["user_id"] = json_value_i64(msg, "user_id");
            p["message"] = normalize_external_text(msg.value("message", ""));
            return accountRef.empty() ? call_onebot_api_bridge("send_private_msg", p) : call_onebot_api_bridge_for_account(accountRef, "send_private_msg", p);
        }
        if (event == "onebot_call") {
            std::string raw = normalize_external_text(msg.value("request", ""));
            if (raw.empty()) return onebot_failed_response(400, "empty onebot request");
            json req = json::parse(raw);
            std::string action = req.value("action", "");
            json params = req.value("params", json::object());
            if (action.empty()) return onebot_failed_response(400, "missing onebot action");
            return accountRef.empty() ? call_onebot_api_bridge(action, params) : call_onebot_api_bridge_for_account(accountRef, action, params);
        }
    } catch (...) {
        return onebot_failed_response(500, "x86 bridge api request exception");
    }
    return onebot_failed_response(400, "unsupported x86 bridge request");
}

static void xlz_bridge_stdout_loop(std::shared_ptr<XlzBridgeRuntime> rt, HANDLE stdoutRead) {
    if (!rt || !stdoutRead) return;
    rt->readerRunning = true;
    std::string buffer;
    char chunk[512];
    DWORD read = 0;
    while (ReadFile(stdoutRead, chunk, sizeof(chunk), &read, NULL) && read > 0) {
        buffer.append(chunk, chunk + read);
        size_t pos = 0;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            try {
                json msg = json::parse(line);
                std::string event = msg.value("event", "");
                { std::lock_guard<std::mutex> lock(g_pluginMutex); rt->lastHostEvent = event; }
                  rt->lastHostAt = (int64_t)time(nullptr);
                  if (event == "ready") {
                    {
                        std::lock_guard<std::mutex> lock(g_pluginMutex);
                        rt->pluginKind = normalize_external_text(msg.value("plugin_type", "xiaolizi-apprun"));
                        rt->hasSettings = msg.value("has_settings", false);
                        rt->hasGroupCallback = msg.value("has_group_callback", false);
                        rt->hasPrivateCallback = msg.value("has_private_callback", false);
                        rt->hasEventCallback = msg.value("has_event_callback", false);
                        rt->hasEnableCallback = msg.value("enable_deferred", false);
                    }
                      rt->appInfoRaw = msg.value("app_info_raw", "");
                    parse_xlz_bridge_appinfo(rt->appInfoRaw, *rt);
                    startup_trace("x86 bridge ready id=%s name=%s kind=%s settings=%d group=%d private=%d event=%d ok=%d",
                                  rt->id.c_str(), rt->name.c_str(), rt->pluginKind.c_str(), rt->hasSettings ? 1 : 0,
                                  rt->hasGroupCallback ? 1 : 0, rt->hasPrivateCallback ? 1 : 0,
                                  rt->hasEventCallback ? 1 : 0, msg.value("ok", true) ? 1 : 0);
                    if (!msg.value("ok", true)) {
                        std::lock_guard<std::mutex> lock(g_pluginMutex);
                        rt->lastError = normalize_external_text(msg.value("warning", msg.value("error", "x86 插件未完整适配")));
                        rt->errorCount++;
                    } else if (msg.contains("warning")) {
                        std::lock_guard<std::mutex> lock(g_pluginMutex);
                        rt->lastError = normalize_external_text(msg.value("warning", ""));
                    }
                    add_log("system", "", "小栗子 x86 插件桥接已就绪: " + rt->name);
                    push_to_frontend("plugins-updated", plugins_to_json().dump());
                } else if (event == "plugin_log") {
                    add_log("plugin", msg.value("plugin", rt->name), normalize_external_text(msg.value("message", "")));
                } else if (event == "error") {
                    {
                        std::lock_guard<std::mutex> lock(g_pluginMutex);
                        rt->lastError = normalize_external_text(msg.value("error", "Host32 error"));
                    }
                    rt->errorCount++;
                    add_log("error", rt->name, "小栗子 x86 宿主错误: " + rt->lastError);
                    push_to_frontend("plugins-updated", plugins_to_json().dump());
                } else if (event == "enable_result") {
                    bool ok = msg.value("ok", false);
                    rt->enableOk = ok;
                    startup_trace("x86 bridge enable_result id=%s ok=%d result=%d available=%d", rt->id.c_str(), ok ? 1 : 0, msg.value("result", 0), msg.value("available", false) ? 1 : 0);
                    if (!ok) {
                        {
                            std::lock_guard<std::mutex> lock(g_pluginMutex);
                            rt->lastError = normalize_external_text(msg.value("error", "插件启用回调返回失败"));
                        }
                        rt->errorCount++;
                        add_log("warning", rt->name, "小栗子 x86 插件启用回调返回失败");
                    }
                    push_to_frontend("plugins-updated", plugins_to_json().dump());
                } else if (event == "dispatch_result") {
                    if (msg.value("ok", false)) rt->dispatchOkCount++;
                    {
                        std::lock_guard<std::mutex> lock(g_pluginMutex);
                        rt->lastEventType = normalize_external_text(msg.value("post_type", rt->lastEventType));
                        rt->lastMessageType = normalize_external_text(msg.value("message_type", rt->lastMessageType));
                    }
                } else if (event == "send_group_msg" || event == "send_private_msg" || event == "onebot_call") {
                    rt->apiCount++;
                    rt->lastApiAt = (int64_t)time(nullptr);
                    {
                        std::lock_guard<std::mutex> lock(g_pluginMutex);
                        rt->lastApiAction = event;
                    }
                    json apiResult = call_xlz_bridge_onebot_request(msg);
                    bool ok = false;
                    if (apiResult.is_object()) {
                        if (apiResult.contains("retcode")) ok = apiResult.value("retcode", -1) == 0;
                        else if (apiResult.contains("status")) ok = apiResult.value("status", "") == "ok";
                    }
                    if (!ok) {
                        std::string reason = apiResult.is_object() ? apiResult.value("message", apiResult.value("msg", apiResult.value("wording", apiResult.dump()))) : apiResult.dump();
                        {
                            std::lock_guard<std::mutex> lock(g_pluginMutex);
                            rt->lastError = normalize_external_text(reason);
                        }
                        rt->errorCount++;
                        add_log("warning", rt->name, "x86 插件 OneBot API 调用失败: " + event + "；" + normalize_external_text(reason));
                    } else {
                        add_log("plugin", rt->name, "x86 插件已调用 OneBot API: " + event);
                    }
                }
            } catch (...) {
                add_log("plugin", rt->name, normalize_external_text(line));
            }
        }
    }
    rt->readerRunning = false;
    CloseHandle(stdoutRead);
    if (!g_shuttingDown.load() && rt->enabled) {
        rt->enabled = false;
        rt->errorCount++;
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            rt->lastError = "Host32 进程已退出";
        }
        add_log("warning", rt->name, "小栗子 x86 宿主进程已退出");
        push_to_frontend("plugins-updated", plugins_to_json().dump());
    }
}

static void stop_xlz_bridge_plugin(const std::shared_ptr<XlzBridgeRuntime>& rt, bool removeFromList) {
    if (!rt) return;
    rt->enabled = false;
    if (rt->stdinWrite) {
        write_xlz_bridge_command(rt, json{{"action", "shutdown"}});
        CloseHandle(rt->stdinWrite);
        rt->stdinWrite = NULL;
    }
    if (rt->process) {
        DWORD wait = WaitForSingleObject(rt->process, 1200);
        if (wait == WAIT_TIMEOUT) TerminateProcess(rt->process, 0);
        CloseHandle(rt->process);
        rt->process = NULL;
    }
    if (rt->stdoutThread.joinable()) rt->stdoutThread.join();
    if (removeFromList) {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        g_xlzBridgePlugins.erase(std::remove_if(g_xlzBridgePlugins.begin(), g_xlzBridgePlugins.end(), [&](const std::shared_ptr<XlzBridgeRuntime>& p) {
            return !p || p->id == rt->id;
        }), g_xlzBridgePlugins.end());
    }
}

static bool start_xlz_bridge_plugin(const ReservedPluginRuntime& reserved, std::string& error) {
    if (reserved.type != "xiaolizi-x86") { error = "不是小栗子 x86 桥接插件"; return false; }
    std::string hostPath = xlz_host32_path();
    if (!file_exists(hostPath)) { error = "未找到 YuexPluginHost32.exe，请放入 main/bin"; return false; }
    startup_trace("x86 bridge start request id=%s path=%s host=%s", reserved.id.c_str(), reserved.path.c_str(), hostPath.c_str());
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        auto existing = find_xlz_bridge_locked(reserved.id);
        if (existing && existing->enabled) return true;
    }
    SECURITY_ATTRIBUTES sa = {}; sa.nLength = sizeof(sa); sa.bInheritHandle = TRUE;
    HANDLE stdinRead = NULL, stdinWrite = NULL, stdoutRead = NULL, stdoutWrite = NULL;
    if (!CreatePipe(&stdinRead, &stdinWrite, &sa, 0) || !CreatePipe(&stdoutRead, &stdoutWrite, &sa, 0)) {
        error = "创建 Host32 管道失败";
        if (stdinRead) CloseHandle(stdinRead); if (stdinWrite) CloseHandle(stdinWrite);
        if (stdoutRead) CloseHandle(stdoutRead); if (stdoutWrite) CloseHandle(stdoutWrite);
        return false;
    }
    SetHandleInformation(stdinWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0);
      STARTUPINFOA si = {}; PROCESS_INFORMATION pi = {}; si.cb = sizeof(si);
      si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = stdinRead; si.hStdOutput = stdoutWrite; si.hStdError = stdoutWrite;
      std::wstring whost = utf8_to_wide(hostPath);
      std::wstring wplugin = utf8_to_wide(reserved.path);
      std::wstring wid = utf8_to_wide(reserved.id);
      std::wstring wcmd = quote_cmd_arg_w(whost) + L" --serve " + quote_cmd_arg_w(wplugin) + L" " + quote_cmd_arg_w(wid);
      std::vector<wchar_t> cmdline(wcmd.begin(), wcmd.end()); cmdline.push_back(L'\0');
      std::string pluginWorkDir = path_parent(reserved.path);
      if (pluginWorkDir.empty()) pluginWorkDir = g_rootDir;
      std::wstring wwork = utf8_to_wide(pluginWorkDir);
      STARTUPINFOW siw = {}; siw.cb = sizeof(siw);
      siw.dwFlags = STARTF_USESTDHANDLES; siw.hStdInput = stdinRead; siw.hStdOutput = stdoutWrite; siw.hStdError = stdoutWrite;
      BOOL ok = CreateProcessW(NULL, cmdline.data(), NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, wwork.empty() ? NULL : wwork.c_str(), &siw, &pi);
    CloseHandle(stdinRead); CloseHandle(stdoutWrite);
    if (!ok) {
        DWORD err = GetLastError(); CloseHandle(stdinWrite); CloseHandle(stdoutRead);
        error = "启动 YuexPluginHost32.exe 失败，错误码 " + std::to_string((unsigned long)err);
        startup_trace("x86 bridge start failed id=%s err=%lu", reserved.id.c_str(), (unsigned long)err);
        return false;
    }
    CloseHandle(pi.hThread);
    auto rt = std::make_shared<XlzBridgeRuntime>();
    rt->id = reserved.id; rt->name = reserved.name; rt->description = reserved.description; rt->path = reserved.path;
    if (rt->name.size() > 6 && rt->name.rfind(" (x86)") == rt->name.size() - 6) rt->name.resize(rt->name.size() - 6);
    rt->pluginKey = reserved.id; rt->enabled = true; rt->processId = pi.dwProcessId; rt->process = pi.hProcess; rt->stdinWrite = stdinWrite;
    rt->stdoutThread = std::thread(xlz_bridge_stdout_loop, rt, stdoutRead);
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        g_xlzBridgePlugins.erase(std::remove_if(g_xlzBridgePlugins.begin(), g_xlzBridgePlugins.end(), [&](const std::shared_ptr<XlzBridgeRuntime>& p) {
            return !p || p->id == reserved.id;
        }), g_xlzBridgePlugins.end());
        g_xlzBridgePlugins.push_back(rt);
    }
    save_plugin_enabled(reserved.id, true);
    add_log("system", "", "已启动小栗子 x86 桥接宿主: " + reserved.name);
    write_xlz_bridge_command(rt, json{{"action", "enable"}});
    return true;
}

static bool set_plugin_enabled_runtime(const std::string& id, bool enabled) {
    { std::lock_guard<std::mutex> lock(g_pluginOpMutex); g_lastPluginOpError.clear(); }
    ReservedPluginRuntime reservedForBridge;
    if (get_reserved_plugin_copy(id, reservedForBridge) && reservedForBridge.type == "xiaolizi-x86") {
        if (enabled) {
            std::string err;
            bool ok = start_xlz_bridge_plugin(reservedForBridge, err);
            if (!ok) {
                add_log("error", reservedForBridge.name, "小栗子 x86 桥接启动失败: " + err);
                std::lock_guard<std::mutex> lock(g_pluginOpMutex);
                g_lastPluginOpError = err;
            }
            return ok;
        }
        std::shared_ptr<XlzBridgeRuntime> bridge;
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            bridge = find_xlz_bridge_locked(id);
        }
        if (bridge) stop_xlz_bridge_plugin(bridge, true);
        save_plugin_enabled(id, false);
        return true;
    }

    YuexPluginInitFn initFn = nullptr;
    YuexPluginShutdownFn shutdownFn = nullptr;
    bool needInit = false;
    bool needShutdown = false;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_plugins) {
            if (p.id != id) continue;
            if (enabled && !p.enabled && p.init) {
                initFn = p.init;
                needInit = true;
            } else if (!enabled && p.enabled && p.shutdown) {
                shutdownFn = p.shutdown;
                needShutdown = true;
            } else {
                save_plugin_enabled(id, p.enabled);
                return true;
            }
            break;
        }
    }
    if (needInit && initFn) {
        bool ok = false;
        bool threw = false;
        auto begin = std::chrono::steady_clock::now();
        try {
            g_pluginCallContextId = id;
            ok = initFn(&g_pluginApi) != 0;
        } catch (...) { ok = false; threw = true; }
        g_pluginCallContextId.clear();
        uint64_t elapsed = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        if (auto p = find_plugin_locked(id)) {
            p->enabled = ok;
            p->lastEventResult = ok ? 1 : 0;
            if (elapsed > p->maxEventMs) p->maxEventMs = elapsed;
            save_plugin_enabled(id, p->enabled);
            if (!ok) {
                p->eventErrorCount++;
                p->lastErrorAt = (int64_t)time(nullptr);
                p->lastError = threw ? "插件初始化异常" : "插件初始化返回失败";
                add_log("error", "", "插件启用失败，" + p->lastError + ": " + p->name);
            }
            return ok;
        }
        return false;
    }
    if (needShutdown && shutdownFn) {
        std::lock_guard<std::mutex> dispatchLock(g_pluginDispatchMutex);
        try {
            g_pluginCallContextId = id;
            shutdownFn();
        } catch (...) {}
        g_pluginCallContextId.clear();
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        if (auto p = find_plugin_locked(id)) {
            p->enabled = false;
            save_plugin_enabled(id, false);
            return true;
        }
        return false;
    }

    XlzPluginRuntime xlz;
    bool foundXlz = false;
    {
        std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
        for (auto it = g_xlzPlugins.begin(); it != g_xlzPlugins.end(); ++it) {
            if (it->id != id) continue;
            xlz = *it;
            g_xlzPlugins.erase(it);
            foundXlz = true;
            break;
        }
    }
    if (foundXlz) {
        if (xlz.enabled && xlz.fnPluginDisable) {
            try { xlz.fnPluginDisable(); } catch (...) {}
        }
        if (xlz.module) {
            FreeLibrary(xlz.module);
            xlz.module = nullptr;
        }

        if (enabled) {
            bool ok = try_load_xlz_plugin(xlz.path, true);
            save_plugin_enabled(id, ok);
            if (!ok) {
                xlz.enabled = false;
                xlz.fnGroupMsg = nullptr;
                xlz.fnPrivateMsg = nullptr;
                xlz.fnEventMsg = nullptr;
                xlz.fnPluginEnable = nullptr;
                xlz.fnPluginDisable = nullptr;
                xlz.fnPluginUninstall = nullptr;
                xlz.fnAppSetting = nullptr;
                std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
                g_xlzPlugins.push_back(xlz);
            }
            return ok;
        }

        xlz.enabled = false;
        xlz.fnGroupMsg = nullptr;
        xlz.fnPrivateMsg = nullptr;
        xlz.fnEventMsg = nullptr;
        xlz.fnPluginEnable = nullptr;
        xlz.fnPluginDisable = nullptr;
        xlz.fnPluginUninstall = nullptr;
        xlz.fnAppSetting = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
            g_xlzPlugins.push_back(xlz);
        }
        save_plugin_enabled(id, false);
        return true;
    }
    save_plugin_enabled(id, enabled);
    return false;
}

static std::wstring plugin_file_basename(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static void unload_all_plugins_for_file_replace() {
    std::vector<std::shared_ptr<XlzBridgeRuntime>> bridges;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        bridges = g_xlzBridgePlugins;
        g_xlzBridgePlugins.clear();
    }
    for (auto& rt : bridges) stop_xlz_bridge_plugin(rt, false);
    std::vector<PluginRuntime> oldPlugins;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        oldPlugins.swap(g_plugins);
    }
    for (auto& p : oldPlugins) unload_plugin(p);
    xlz_unload_all();
}

static json install_plugin_from_dialog() {
    ensure_directory_tree(g_pluginDir);
    wchar_t selected[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_mainHwnd;
    ofn.lpstrFile = selected;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"DLL 插件 (*.dll)\0*.dll\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrTitle = L"选择要载入的插件 DLL";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    json r;
    if (!GetOpenFileNameW(&ofn)) {
        r["ok"] = false;
        r["cancelled"] = true;
        return r;
    }

    std::wstring fileName = plugin_file_basename(selected);
    if (fileName.empty()) {
        r["ok"] = false;
        r["error"] = "插件文件名无效";
        return r;
    }

    std::wstring wPluginDir = utf8_to_wide(g_pluginDir);
    if (!wPluginDir.empty() && wPluginDir.back() != L'\\' && wPluginDir.back() != L'/') wPluginDir += L"\\";
    std::wstring dest = wPluginDir + fileName;
    unload_all_plugins_for_file_replace();
    BOOL copied = CopyFileW(selected, dest.c_str(), FALSE);
    if (!copied) {
        DWORD err = GetLastError();
        r["ok"] = false;
        r["error"] = "复制插件失败，错误码 " + std::to_string((unsigned long)err);
        scan_plugins();
        return r;
    }

    scan_plugins();
    std::string destUtf8 = normalize_external_text(wide_to_utf8(dest));
    std::string fileNameUtf8 = normalize_external_text(wide_to_utf8(fileName));
    bool nativeLoaded = false;
    std::string failReason;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (const auto& p : g_plugins) {
            if (p.path == destUtf8 || plugin_stem_from_path(p.path) == plugin_stem_from_path(destUtf8)) {
                nativeLoaded = true;
                break;
            }
        }
        if (!nativeLoaded) {
            for (const auto& p : g_reservedPlugins) {
                if (p.path == destUtf8 || plugin_stem_from_path(p.path) == plugin_stem_from_path(destUtf8)) {
                    failReason = p.reason;
                    break;
                }
            }
        }
    }
    if (!nativeLoaded) {
        if (failReason.empty()) failReason = "不是 YuexBot 原生插件，或缺少 yuex_plugin_* 导出函数";
        bool reservedX86 = false;
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            for (const auto& p : g_reservedPlugins) {
                if ((p.path == destUtf8 || plugin_stem_from_path(p.path) == plugin_stem_from_path(destUtf8)) && p.type == "xiaolizi-x86") {
                    reservedX86 = true;
                    break;
                }
            }
        }
        if (reservedX86) {
            r["ok"] = true;
            r["path"] = destUtf8;
            r["name"] = fileNameUtf8;
            r["pending_host"] = true;
            r["message"] = "已添加小栗子 x86 插件，可通过 YuexPluginHost32.exe 宿主桥接运行";
            add_log("warning", "", r.value("message", "") + ": " + fileNameUtf8);
            return r;
        }
        DeleteFileW(dest.c_str());
        scan_plugins();
        r["ok"] = false;
        r["error"] = "插件未载入: " + fileNameUtf8 + "；原因: " + failReason;
        add_log("error", "", r.value("error", ""));
        return r;
    }

    r["ok"] = true;
    r["path"] = destUtf8;
    r["name"] = fileNameUtf8;
    add_log("system", "", "已添加插件: " + r.value("name", ""));
    return r;
}

static json open_plugin_settings_runtime(const std::string& id) {
    int (YUEX_PLUGIN_CALL *nativeSettings)() = nullptr;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& p : g_plugins) {
            if (p.id != id) continue;
            nativeSettings = p.openSettings;
            name = p.name;
            break;
        }
    }
    if (nativeSettings) {
        try {
            g_pluginCallContextId = id;
            nativeSettings();
            g_pluginCallContextId.clear();
            return json{{"ok", true}};
        } catch (...) {
            g_pluginCallContextId.clear();
            return json{{"ok", false}, {"error", "插件设置窗口打开失败"}};
        }
    }
    if (!name.empty()) {
        return json{{"ok", false}, {"error", "该 YuexBot 插件没有提供设置窗口"}};
    }

    {
        std::shared_ptr<XlzBridgeRuntime> bridge;
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            bridge = find_xlz_bridge_locked(id);
        }
        if (bridge) {
            if (!bridge->enabled || !bridge->stdinWrite) return json{{"ok", false}, {"error", "请先启用 x86 插件后再打开设置"}};
            bool ok = write_xlz_bridge_command(bridge, json{{"action", "settings"}});
            return ok ? json{{"ok", true}, {"message", "已请求打开 x86 插件设置窗口"}}
                      : json{{"ok", false}, {"error", "x86 插件设置命令发送失败"}};
        }
    }

    int (*xlzSettings)() = nullptr;
    bool foundXlz = false;
    bool enabled = false;
    {
        std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
        for (auto& p : g_xlzPlugins) {
            if (p.id != id) continue;
            foundXlz = true;
            enabled = p.enabled;
            xlzSettings = p.fnAppSetting;
            name = p.name;
            break;
        }
    }
    if (foundXlz && !enabled) {
        return json{{"ok", false}, {"error", "请先启用插件后再打开设置"}};
    }
    if (xlzSettings) {
        try {
            xlzSettings();
            return json{{"ok", true}};
        } catch (...) {
            return json{{"ok", false}, {"error", "XiaoLiZi 插件设置窗口打开失败"}};
        }
    }
    if (foundXlz) return json{{"ok", false}, {"error", "该插件没有提供设置窗口"}};
    return json{{"ok", false}, {"error", "未找到插件"}};
}

static json delete_plugin_runtime(const std::string& id) {
    if (id.empty()) return json{{"ok", false}, {"error", "插件 ID 为空"}};
    std::string path;
    bool found = false;
    PluginRuntime removedPlugin;
    bool hasRemovedPlugin = false;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto it = g_plugins.begin(); it != g_plugins.end(); ++it) {
            if (it->id != id) continue;
            path = it->path;
            removedPlugin = *it;
            g_plugins.erase(it);
            found = true;
            hasRemovedPlugin = true;
            break;
        }
    }
    if (hasRemovedPlugin) unload_plugin(removedPlugin);

    if (!found) {
        std::lock_guard<std::mutex> lock(g_xlzPluginMutex);
        for (auto it = g_xlzPlugins.begin(); it != g_xlzPlugins.end(); ++it) {
            if (it->id != id) continue;
            path = it->path;
            if (it->enabled && it->fnPluginDisable) {
                try { it->fnPluginDisable(); } catch (...) {}
            }
            if (it->fnPluginUninstall) {
                try { it->fnPluginUninstall(); } catch (...) {}
            }
            if (it->module) FreeLibrary(it->module);
            g_xlzPlugins.erase(it);
            found = true;
            break;
        }
    }

    if (!found) {
        std::shared_ptr<XlzBridgeRuntime> bridge;
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            bridge = find_xlz_bridge_locked(id);
            if (bridge) {
                path = bridge->path;
                found = true;
            }
        }
        if (bridge) stop_xlz_bridge_plugin(bridge, true);
    }

    if (!found) {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto it = g_reservedPlugins.begin(); it != g_reservedPlugins.end(); ++it) {
            if (it->id != id) continue;
            path = it->path;
            g_reservedPlugins.erase(it);
            found = true;
            break;
        }
    }

    if (!found || path.empty()) return json{{"ok", false}, {"error", "未找到插件"}};
    save_plugin_enabled(id, false);

    BOOL deleted = FALSE;
    std::wstring wpath = utf8_to_wide(path);
    if (!wpath.empty()) deleted = DeleteFileW(wpath.c_str());
    if (!deleted) deleted = DeleteFileA(path.c_str());
    if (!deleted) {
        DWORD err = GetLastError();
        scan_plugins();
        return json{{"ok", false}, {"error", "删除插件失败，错误码 " + std::to_string((unsigned long)err)}};
    }

    scan_plugins();
    add_log("system", "", "已删除插件: " + id);
    return json{{"ok", true}};
}

static void dispatch_plugin_event_now(int type, const std::string& body) {
    if (type != 0) {
        std::lock_guard<std::mutex> dispatchLock(g_pluginDispatchMutex);
        struct Callback {
            std::string id;
            std::string name;
            YuexPluginOnEventFn fn = nullptr;
        };
        std::vector<Callback> callbacks;
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            for (auto& p : g_plugins) {
                if (!p.enabled || !p.onEvent) continue;
                if ((p.eventMask & (1u << type)) == 0) continue;
                callbacks.push_back({p.id, p.name, p.onEvent});
            }
        }
        for (auto& cb : callbacks) {
            int result = 0;
            bool failed = false;
            std::string errorText;
            auto begin = std::chrono::steady_clock::now();
            try {
                g_pluginCallContextId = cb.id;
                result = cb.fn(type, body.c_str());
            } catch (...) {
                failed = true;
                errorText = "插件事件回调异常";
                add_log("system", "", errorText + ": " + cb.name);
            }
            g_pluginCallContextId.clear();
            auto end = std::chrono::steady_clock::now();
            uint64_t elapsed = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
            {
                std::lock_guard<std::mutex> lock(g_pluginMutex);
                if (auto* p = find_plugin_locked(cb.id)) {
                    p->eventCount++;
                    p->lastEventAt = (int64_t)time(nullptr);
                    p->lastEventResult = result;
                    p->totalEventMs += elapsed;
                    if (elapsed > p->maxEventMs) p->maxEventMs = elapsed;
                    if (elapsed >= 1000) p->slowEventCount++;
                    if (failed) {
                        p->eventErrorCount++;
                        p->lastErrorAt = p->lastEventAt;
                        p->lastError = errorText.empty() ? "插件回调失败" : errorText;
                    }
                }
            }
            if (elapsed >= 1000 && (elapsed >= 3000 || (time(nullptr) % 20 == 0))) {
                add_log("warning", "", "插件回调耗时较高: " + cb.name + " " + std::to_string(elapsed) + "ms");
            }
        }
    }
    if (g_xiaoliziCompatEnabled) {
        try {
            json evt = json::parse(body);
            xlz_dispatch_plugin_event(evt);
        } catch (...) {
            add_log("system", "", "XiaoLiZi plugin event dispatch exception");
        }
    }
    std::vector<std::shared_ptr<XlzBridgeRuntime>> bridges;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        for (auto& rt : g_xlzBridgePlugins) {
            if (rt && rt->enabled && rt->stdinWrite) bridges.push_back(rt);
        }
    }
    if (!bridges.empty()) {
        json evt;
        try { evt = json::parse(body); } catch (...) { evt = json::object(); }
        if (evt.is_object()) {
            std::string postType = normalize_external_text(evt.value("post_type", ""));
            std::string messageType = normalize_external_text(evt.value("message_type", ""));
            std::string noticeType = normalize_external_text(evt.value("notice_type", ""));
            for (auto& rt : bridges) {
                rt->dispatchCount++;
                rt->lastEventAt = (int64_t)time(nullptr);
                {
                    std::lock_guard<std::mutex> lock(g_pluginMutex);
                    rt->lastEventType = postType;
                    rt->lastMessageType = messageType;
                    rt->lastNoticeType = noticeType;
                }
                if (!write_xlz_bridge_command(rt, json{{"action", "event"}, {"event", evt}})) {
                    rt->errorCount++;
                    std::lock_guard<std::mutex> lock(g_pluginMutex);
                    rt->lastError = "写入 Host32 管道失败";
                }
            }
        }
    }
}

static void plugin_event_worker_loop() {
    startup_trace("plugin event worker started");
    while (true) {
        PluginEventJob job;
        {
            std::unique_lock<std::mutex> lock(g_pluginEventMutex);
            g_pluginEventCv.wait(lock, [] {
                return !g_pluginEventWorkerRunning.load() || !g_pluginEventQueue.empty();
            });
            if (!g_pluginEventWorkerRunning.load() && g_pluginEventQueue.empty()) break;
            job = std::move(g_pluginEventQueue.front());
            g_pluginEventQueue.pop_front();
        }
        dispatch_plugin_event_now(job.type, job.body);
    }
    startup_trace("plugin event worker stopped");
}

static void start_plugin_event_worker() {
    bool expected = false;
    if (!g_pluginEventWorkerRunning.compare_exchange_strong(expected, true)) return;
    g_pluginEventThread = std::thread(plugin_event_worker_loop);
}

static void stop_plugin_event_worker() {
    bool expected = true;
    if (!g_pluginEventWorkerRunning.compare_exchange_strong(expected, false)) return;
    {
        std::lock_guard<std::mutex> lock(g_pluginEventMutex);
        g_pluginEventQueue.clear();
    }
    g_pluginEventCv.notify_all();
    if (g_pluginEventThread.joinable()) g_pluginEventThread.join();
}

static void stop_all_xlz_bridge_plugins() {
    std::vector<std::shared_ptr<XlzBridgeRuntime>> bridges;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        bridges = g_xlzBridgePlugins;
        g_xlzBridgePlugins.clear();
    }
    for (auto& rt : bridges) stop_xlz_bridge_plugin(rt, false);
}

static bool plugin_has_event_subscriber(int type) {
    if (type == 0) return false;
    if (g_xiaoliziCompatEnabled) return true;
    std::lock_guard<std::mutex> lock(g_pluginMutex);
    for (auto& rt : g_xlzBridgePlugins) {
        if (rt && rt->enabled && rt->stdinWrite) return true;
    }
    for (auto& p : g_plugins) {
        if (!p.enabled || !p.onEvent) continue;
        if ((p.eventMask & (1u << type)) != 0) return true;
    }
    return false;
}

static void dispatch_plugin_event(const json& evt) {
    int type = event_type_to_plugin(json_value_string(evt, "post_type"));
    if (type == 0 || g_shuttingDown.load()) return;
    if (!plugin_has_event_subscriber(type)) return;
    PluginEventJob job;
    job.type = type;
    job.body = evt.dump();
    bool dropped = false;
    {
        std::lock_guard<std::mutex> lock(g_pluginEventMutex);
        if (!g_pluginEventWorkerRunning.load()) {
            dispatch_plugin_event_now(job.type, job.body);
            return;
        }
        if (g_pluginEventQueue.size() >= kPluginEventQueueLimit) {
            g_pluginEventQueue.pop_front();
            g_pluginEventsDropped++;
            dropped = true;
        }
        g_pluginEventQueue.push_back(std::move(job));
        g_pluginEventsQueued++;
    }
    g_pluginEventCv.notify_one();
    if (dropped && (g_pluginEventsDropped.load() == 1 || g_pluginEventsDropped.load() % 100 == 0)) {
        add_log("warning", "", "插件事件队列繁忙，已丢弃较早事件 " + std::to_string(g_pluginEventsDropped.load()) + " 条");
    }
}

static void shutdown_runtime() {
    if (g_shuttingDown.exchange(true)) return;
    startup_trace("shutdown_runtime begin");
    g_autoReconnect = false;
    g_connected = false;
    g_eventServerRunning = false;
    close_event_server_socket();
    g_reverseWsListening = false;
    {
        std::lock_guard<std::mutex> lock(g_reverseWsMutex);
        if (g_reverseWsSocket != INVALID_SOCKET) {
            closesocket(g_reverseWsSocket);
            g_reverseWsSocket = INVALID_SOCKET;
        }
    }
    try { if (g_ws.isConnected()) g_ws.disconnect(); } catch (...) {}
    try { g_onebot.disconnect(); } catch (...) {}
    stop_plugin_event_worker();
    stop_all_xlz_bridge_plugins();
    std::vector<PluginRuntime> oldPlugins;
    {
        std::lock_guard<std::mutex> lock(g_pluginMutex);
        oldPlugins.swap(g_plugins);
    }
    for (auto& p : oldPlugins) unload_plugin(p);
    xlz_unload_all();
    if (cleanup_all_windows) cleanup_all_windows();
    startup_trace("shutdown_runtime end");
}

static void request_process_exit(const char* reason) {
    if (g_exitRequested.exchange(true)) return;
    startup_trace("process exit requested: %s", reason ? reason : "unknown");
    std::thread([] {
        Sleep(1800);
        startup_trace("forced TerminateProcess");
        TerminateProcess(GetCurrentProcess(), 0);
    }).detach();
    shutdown_runtime();
    startup_trace("ExitProcess now");
    ExitProcess(0);
}

static std::string event_sender_display(const json& e, int64_t user_id) {
    if (e.contains("sender") && e["sender"].is_object()) {
        std::string name = json_value_string(e["sender"], "card");
        if (name.empty()) name = json_value_string(e["sender"], "nickname");
        if (!name.empty()) return name;
    }
    return user_id > 0 ? std::to_string(user_id) : "";
}

static json normalize_plugin_event(json evt, const std::string& accountId, const std::string& accountName) {
    if (!evt.is_object()) return evt;
    if (!accountId.empty()) evt["account_id"] = accountId;
    if (!accountName.empty()) evt["account_name"] = accountName;
    evt["self_id"] = json_value_string(evt, "self_id");

    std::string postType = json_value_string(evt, "post_type");
    int64_t userId = json_value_i64(evt, "user_id");
    int64_t groupId = json_value_i64(evt, "group_id");
    if (userId > 0) evt["sender_id"] = userId;
    if (groupId > 0) evt["group_id"] = groupId;
    evt["target_id"] = json_value_i64(evt, "target_id");
    evt["operator_id"] = json_value_i64(evt, "operator_id");
    evt["message_id"] = json_value_string(evt, "message_id");

    if (postType == "message" || postType == "message_sent") {
        evt["message_type"] = json_value_string(evt, "message_type");
        evt["sub_type"] = json_value_string(evt, "sub_type");
        std::string senderName = json_value_string(evt, "sender_name");
        if (senderName.empty()) senderName = event_sender_display(evt, userId);
        evt["sender_name"] = senderName;
        std::string groupName = json_value_string(evt, "group_name");
        if (groupName.empty()) groupName = cached_group_name_for_account(accountId, groupId);
        evt["group_name"] = groupName;
    } else if (postType == "notice") {
        evt["notice_type"] = json_value_string(evt, "notice_type");
        evt["sub_type"] = json_value_string(evt, "sub_type");
        std::string groupName = json_value_string(evt, "group_name");
        if (groupName.empty()) groupName = cached_group_name_for_account(accountId, groupId);
        evt["group_name"] = groupName;
        std::string senderName = json_value_string(evt, "sender_name");
        if (senderName.empty()) senderName = event_sender_display(evt, userId);
        evt["sender_name"] = senderName;
    } else if (postType == "request") {
        evt["request_type"] = json_value_string(evt, "request_type");
        evt["sub_type"] = json_value_string(evt, "sub_type");
        std::string groupName = json_value_string(evt, "group_name");
        if (groupName.empty()) groupName = cached_group_name_for_account(accountId, groupId);
        evt["group_name"] = groupName;
        std::string senderName = json_value_string(evt, "sender_name");
        if (senderName.empty()) senderName = event_sender_display(evt, userId);
        evt["sender_name"] = senderName;
    } else if (postType == "meta_event") {
        evt["sub_type"] = json_value_string(evt, "meta_event_type");
    }
    return evt;
}

static void process_event(json evt) {
    if (!evt.is_object()) return;
    try {
        std::string accountId = json_value_string(evt, "account_id");
        if (accountId.empty()) accountId = find_account_id_by_self_id(json_value_string(evt, "self_id"));
        if (accountId.empty()) accountId = g_activeAccountId;
        std::string accountName = account_display_name(accountId);
        if (!accountId.empty()) {
            evt["account_id"] = accountId;
            if (!accountName.empty()) evt["account_name"] = accountName;
            std::string selfId = json_value_string(evt, "self_id");
            if (!selfId.empty()) update_account_runtime_login(accountId, selfId, "");
            auto rt = get_account_runtime(accountId);
            if (rt) rt->lastEventTime = (int64_t)time(nullptr);
        }
        evt = normalize_plugin_event(evt, accountId, accountName);
        dispatch_plugin_event(evt);
        std::string post_type = json_value_string(evt, "post_type");
        g_totalEvents++;
        json baseDetail;
        baseDetail["post_type"] = post_type;
        baseDetail["self_id"] = json_value_string(evt, "self_id");

        auto message_to_text = [](const json& e) -> std::string {
            std::string text = json_value_string(e, "raw_message");
            if (!text.empty()) return text;
            if (!e.contains("message")) return "";
            const auto& msg = e["message"];
            if (msg.is_string()) return msg.get<std::string>();
            if (!msg.is_array()) return msg.dump();
            std::string out;
            for (const auto& seg : msg) {
                std::string type = json_value_string(seg, "type");
                const json* data = (seg.contains("data") && seg["data"].is_object()) ? &seg["data"] : nullptr;
                if (type == "text" && data) out += json_value_string(*data, "text");
                else if (type == "at" && data) out += "@" + json_value_string(*data, "qq") + " ";
                else if (type == "face" && data) out += "[表情:" + json_value_string(*data, "id") + "]";
                else if (type == "image") out += "[图片]";
                else if (type == "record") out += "[语音]";
                else if (type == "video") out += "[视频]";
                else if (type == "file") out += "[文件]";
                else if (type == "reply" && data) out += "[回复:" + json_value_string(*data, "id") + "]";
                else if (!type.empty()) out += "[" + type + "]";
            }
            return out;
        };

        if (post_type == "message" || post_type == "message_sent") {
            bool isSent = post_type == "message_sent";
            std::string msg_type = json_value_string(evt, "message_type");
            std::string raw_msg = message_to_text(evt);
            if (raw_msg.empty()) raw_msg = "[空消息]";
            if (isSent) raw_msg = "[自己发送] " + raw_msg;
            int64_t user_id = json_value_i64(evt, "user_id");
            int64_t group_id = json_value_i64(evt, "group_id");
            std::string sender_name = json_value_string(evt, "sender_name");
            if (sender_name.empty()) sender_name = event_sender_display(evt, user_id);
            if (sender_name.empty()) sender_name = "未知发送者";
            g_totalMessages++;
            json detail = baseDetail;
            detail["message_type"] = msg_type;
            detail["sub_type"] = json_value_string(evt, "sub_type");
            detail["sender_name"] = sender_name;
            detail["message_id"] = json_value_string(evt, "message_id");

            if (msg_type == "group") {
                std::string group_name = json_value_string(evt, "group_name");
                if (group_name.empty()) group_name = cached_group_name_for_account(accountId, group_id);
                detail["group_name"] = group_name;
                std::string display = sender_name;
                if (!group_name.empty()) display += " @ " + group_name + "(" + std::to_string(group_id) + ")";
                else if (group_id > 0) display += " @ 群 " + std::to_string(group_id);
                if (!accountName.empty()) display = accountName + " · " + display;
                add_log("group", display, raw_msg, user_id, group_id, accountId, accountName, detail);
            } else if (msg_type == "private") {
                std::string display = accountName.empty() ? sender_name : accountName + " · " + sender_name;
                add_log("private", display, raw_msg, user_id, 0, accountId, accountName, detail);
            } else {
                std::string display = accountName.empty() ? sender_name : accountName + " · " + sender_name;
                add_log("system", display, "[" + msg_type + "] " + raw_msg, user_id, group_id, accountId, accountName, detail);
            }
        } else if (post_type == "notice") {
            std::string notice_type = json_value_string(evt, "notice_type");
            std::string sub_type = json_value_string(evt, "sub_type");
            int64_t user_id = json_value_i64(evt, "user_id");
            int64_t group_id = json_value_i64(evt, "group_id");
            json detail = baseDetail;
            detail["notice_type"] = notice_type;
            detail["sub_type"] = sub_type;
            detail["target_id"] = json_value_i64(evt, "target_id");
            detail["operator_id"] = json_value_i64(evt, "operator_id");
            detail["message_id"] = json_value_string(evt, "message_id");
            std::string group_name = cached_group_name_for_account(accountId, group_id);
            detail["group_name"] = group_name;
            std::string msg;
            if (notice_type == "group_increase") {
                msg = "群成员增加: " + std::to_string(user_id) + " 加入群 " + std::to_string(group_id);
            } else if (notice_type == "group_decrease") {
                msg = "群成员减少: " + std::to_string(user_id) + " 离开群 " + std::to_string(group_id);
            } else if (notice_type == "group_admin") {
                msg = "群管理员变更: " + std::to_string(user_id) + " " + sub_type + " @ " + std::to_string(group_id);
            } else if (notice_type == "group_ban") {
                int64_t duration = json_value_i64(evt, "duration");
                msg = "群禁言变更: " + std::to_string(user_id) + " 时长 " + std::to_string(duration) + " 秒 @ " + std::to_string(group_id);
            } else if (notice_type == "friend_add") {
                msg = "好友新增: " + std::to_string(user_id);
            } else if (notice_type == "group_recall") {
                msg = "群消息撤回: message_id=" + json_value_string(evt, "message_id") + " @ " + std::to_string(group_id);
            } else if (notice_type == "friend_recall") {
                msg = "私聊消息撤回: message_id=" + json_value_string(evt, "message_id");
            } else if (notice_type == "notify") {
                if (sub_type == "poke") msg = "戳一戳: " + json_value_string(evt, "user_id") + " -> " + json_value_string(evt, "target_id");
                else msg = "通知事件: notify/" + sub_type;
            } else {
                msg = "通知事件: " + notice_type + (sub_type.empty() ? "" : "/" + sub_type);
            }
            add_log("notice", accountName, msg, user_id, group_id, accountId, accountName, detail);
        } else if (post_type == "request") {
            std::string request_type = json_value_string(evt, "request_type");
            int64_t user_id = json_value_i64(evt, "user_id");
            int64_t group_id = json_value_i64(evt, "group_id");
            std::string comment = json_value_string(evt, "comment");
            json detail = baseDetail;
            detail["request_type"] = request_type;
            detail["sub_type"] = json_value_string(evt, "sub_type");
            detail["group_name"] = cached_group_name_for_account(accountId, group_id);
            detail["flag"] = json_value_string(evt, "flag");
            detail["comment"] = comment;
            std::string msg;
            if (request_type == "friend") {
                msg = "好友请求: " + std::to_string(user_id);
            } else if (request_type == "group") {
                msg = "群请求: " + std::to_string(user_id) + " -> " + std::to_string(group_id) + " (" + json_value_string(evt, "sub_type") + ")";
            } else {
                msg = "请求事件: " + request_type;
            }
            if (!comment.empty()) msg += " 备注: " + comment;
            add_log("request", accountName, msg, user_id, group_id, accountId, accountName, detail);
        } else if (post_type == "meta_event") {
            std::string meta_type = json_value_string(evt, "meta_event_type");
            json detail = baseDetail;
            detail["sub_type"] = meta_type;
            if (meta_type != "heartbeat") add_log("meta", accountName, "元事件: " + meta_type, 0, 0, accountId, accountName, detail);
        } else {
            std::string rawEvent = evt.dump();
            json detail = baseDetail;
            detail["raw_event"] = rawEvent;
            add_log("system", accountName, "未识别事件: " + rawEvent, 0, 0, accountId, accountName, detail);
        }
    } catch (const std::exception& e) {
        add_log("system", "", std::string("运行异常: ") + e.what());
        startup_trace("process_event exception=%s", e.what());
    } catch (...) {
        add_log("system", "", "事件解析处理异常: unknown");
        startup_trace("process_event unknown exception");
    }
}

static void event_server_thread() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == INVALID_SOCKET) return;
    {
        std::lock_guard<std::mutex> lock(g_eventServerMutex);
        g_eventServerSocket = server_fd;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(g_eventServerPort);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        {
            std::lock_guard<std::mutex> lock(g_eventServerMutex);
            if (g_eventServerSocket == server_fd) g_eventServerSocket = INVALID_SOCKET;
        }
        closesocket(server_fd);
        WSACleanup();
        return;
    }
    listen(server_fd, 5);
    g_eventServerRunning = true;
    add_log("system", "", "HTTP 事件上报服务已启动，端口: " + std::to_string(g_eventServerPort));

    while (g_eventServerRunning) {
        struct sockaddr_in client_addr;
        int client_len = sizeof(client_addr);
        SOCKET client = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client == INVALID_SOCKET) {
            if (!g_eventServerRunning.load() || g_shuttingDown.load()) break;
            continue;
        }
        if (g_shuttingDown.load()) { closesocket(client); break; }

        // Read HTTP request
        char buf[8192];
        int n = recv(client, buf, sizeof(buf) - 1, 0);
        if (n <= 0) { closesocket(client); continue; }
        buf[n] = '\0';
        std::string request(buf, n);

        // Dedicated reverse-ws is handled by do_connect(). The HTTP event
        // server should reject WS upgrades so two listeners do not fight over
        // LLBot/NapCat reverse connections.
        if (request.find("Upgrade: websocket") != std::string::npos ||
            request.find("upgrade: websocket") != std::string::npos) {
            const char* resp = "HTTP/1.1 426 Upgrade Required\r\nContent-Length: 0\r\n\r\n";
            send(client, resp, (int)strlen(resp), 0);
            closesocket(client);
            continue;
        }

        // Regular HTTP POST event
        size_t body_start = request.find("\r\n\r\n");
        if (body_start != std::string::npos) {
            std::string body = normalize_external_text(request.substr(body_start + 4));
            try {
                auto evt = json::parse(body);
                process_event(evt);
            } catch (...) {}
        }

        // Send HTTP 200 response
        const char* resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}";
        send(client, resp, (int)strlen(resp), 0);
        closesocket(client);
    }
    bool closeLocalSocket = false;
    {
        std::lock_guard<std::mutex> lock(g_eventServerMutex);
        if (g_eventServerSocket == server_fd) {
            g_eventServerSocket = INVALID_SOCKET;
            closeLocalSocket = true;
        }
    }
    if (closeLocalSocket) closesocket(server_fd);
    WSACleanup();
}

// ============================================================
// Connection Management
// ============================================================
static bool extract_onebot_event(json evt, json& out) {
    if (!evt.is_object()) return false;
    if (evt.contains("post_type")) {
        out = evt;
        return true;
    }
    if (evt.contains("params") && evt["params"].is_object() && evt["params"].contains("post_type")) {
        out = evt["params"];
        return true;
    }
    if (evt.contains("data") && evt["data"].is_object() && evt["data"].contains("post_type")) {
        out = evt["data"];
        return true;
    }
    if (evt.contains("event") && evt["event"].is_object() && evt["event"].contains("post_type")) {
        out = evt["event"];
        return true;
    }
    if (evt.contains("payload") && evt["payload"].is_object() && evt["payload"].contains("post_type")) {
        out = evt["payload"];
        return true;
    }
    if (evt.contains("message_type")) {
        evt["post_type"] = "message";
        out = evt;
        return true;
    }
    if (evt.value("type", "") == "message" || evt.value("type", "") == "message_created") {
        evt["post_type"] = "message";
        if (!evt.contains("message_type")) {
            std::string detail = evt.value("detail_type", evt.value("channel_type", ""));
            evt["message_type"] = (detail == "group" || evt.contains("group_id")) ? "group" : "private";
        }
        out = evt;
        return true;
    }
    if (evt.contains("notice_type")) {
        evt["post_type"] = "notice";
        out = evt;
        return true;
    }
    if (evt.contains("request_type")) {
        evt["post_type"] = "request";
        out = evt;
        return true;
    }
    if (evt.contains("meta_event_type")) {
        evt["post_type"] = "meta_event";
        out = evt;
        return true;
    }
    return false;
}

static void setup_ws_handlers() {
    g_ws.onMessage = [](const std::string& msg) {
        try {
            std::string text = normalize_external_text(msg);
            auto evt = json::parse(text);
            startup_trace("ws recv: %s", text.substr(0, 200).c_str());
            auto activeRt = get_account_runtime(g_activeAccountId);
            if (activeRt) {
                activeRt->wsMessages++;
                activeRt->lastWsPreview = text.size() > kMaxWsPreviewChars ? text.substr(0, kMaxWsPreviewChars) + "..." : text;
            }
            auto handleEventObject = [](const json& one) -> bool {
                json eventPayload;
                if (extract_onebot_event(one, eventPayload)) {
                    auto rt = get_account_runtime(g_activeAccountId);
                    if (rt) rt->wsEvents++;
                    process_event(eventPayload);
                    return true;
                }
                return false;
            };
            bool handled = false;
            if (evt.is_array()) {
                for (const auto& item : evt) {
                    if (handleEventObject(item)) handled = true;
                }
                if (handled) return;
            }
            if (handleEventObject(evt)) {
                return;
            } else if (evt.contains("echo")) {
                // API response - store for callApiViaWs
                std::string echo = json_value_string(evt, "echo");
                std::lock_guard<std::mutex> lock(g_wsResponseMutex);
                g_wsResponses[echo] = evt;
                trim_ws_response_cache_locked(g_wsResponses);
            } else if (evt.contains("retcode") || evt.contains("status") || evt.contains("data")) {
                startup_trace("ws api-like response ignored without echo");
            } else {
                std::string preview = text.size() > 300 ? text.substr(0, 300) + "..." : text;
                add_log("warning", "", "收到未识别 WS 数据: " + preview);
            }
        } catch (const std::exception& e) {
            add_log("error", "", std::string("WS 消息解析失败: ") + e.what());
        } catch (...) {
            add_log("error", "", "WS 消息解析失败: unknown");
        }
    };
    g_ws.onConnect = []() {
        add_log("system", "", "[" + g_connMode + "] WebSocket connected");
    };
    g_ws.onDisconnect = []() {
        add_log("system", "", "[" + g_connMode + "] WebSocket disconnected");
        if (g_connected) {
            g_connected = false;
            g_autoReconnect = true;
            json status;
            status["connected"] = false;
            status["error"] = "WS disconnected";
            push_to_frontend("status-changed", status.dump());
        }
    };
    g_ws.onError = [](const std::string& err) {
        add_log("system", "", "[" + g_connMode + "] WebSocket 错误: " + err);
        return;
    };
}

static void setup_runtime_ws_handlers(const std::shared_ptr<AccountRuntime>& rt) {
    if (!rt || !rt->ws) return;
    std::weak_ptr<AccountRuntime> weak = rt;
    rt->ws->onMessage = [weak](const std::string& msg) {
        auto self = weak.lock();
        if (!self) return;
        try {
            std::string text = normalize_external_text(msg);
            self->wsMessages++;
            self->lastWsPreview = text.size() > kMaxWsPreviewChars ? text.substr(0, kMaxWsPreviewChars) + "..." : text;
            startup_trace("runtime ws recv account=%s count=%d text=%s", self->config.id.c_str(), self->wsMessages.load(), self->lastWsPreview.c_str());
            auto evt = json::parse(text);
            auto handleEventObject = [&](const json& one) -> bool {
                json eventPayload;
                if (extract_onebot_event(one, eventPayload)) {
                    eventPayload["account_id"] = self->config.id;
                    self->wsEvents++;
                    startup_trace("runtime ws event account=%s post_type=%s message_type=%s",
                                  self->config.id.c_str(),
                                  json_value_string(eventPayload, "post_type").c_str(),
                                  json_value_string(eventPayload, "message_type").c_str());
                    process_event(eventPayload);
                    return true;
                }
                return false;
            };
            bool handled = false;
            if (evt.is_array()) {
                for (const auto& item : evt) {
                    if (handleEventObject(item)) handled = true;
                }
                if (handled) return;
            }
            if (handleEventObject(evt)) {
                return;
            } else if (evt.contains("echo")) {
                std::string echo = json_value_string(evt, "echo");
                std::lock_guard<std::mutex> lock(self->wsResponseMutex);
                self->wsResponses[echo] = evt;
                trim_ws_response_cache_locked(self->wsResponses);
            } else if (evt.contains("retcode") || evt.contains("status") || evt.contains("data")) {
                startup_trace("runtime ws api-like response ignored without echo account=%s", self->config.id.c_str());
            } else {
                self->wsUnknownMessages++;
                std::string preview = text.size() > 300 ? text.substr(0, 300) + "..." : text;
                self->lastWsPreview = preview;
                add_log("warning", account_display_name(self->config.id), "收到未识别 WS 数据: " + preview, 0, 0, self->config.id, account_display_name(self->config.id));
            }
        } catch (const std::exception& e) {
            add_log("error", account_display_name(self->config.id), std::string("WS 消息解析失败: ") + e.what(), 0, 0, self->config.id, account_display_name(self->config.id));
        } catch (...) {
            add_log("error", account_display_name(self->config.id), "WS 消息解析失败: unknown", 0, 0, self->config.id, account_display_name(self->config.id));
        }
    };
    rt->ws->onConnect = [weak]() {
        auto self = weak.lock();
        if (!self) return;
        add_log("system", account_display_name(self->config.id), "[" + self->mode + "] WebSocket connected", 0, 0, self->config.id, account_display_name(self->config.id));
    };
    rt->ws->onDisconnect = [weak]() {
        auto self = weak.lock();
        if (!self) return;
        self->connected = false;
        self->connecting = false;
        update_account_runtime_status(self->config.id, "offline");
        add_log("system", account_display_name(self->config.id), "[" + self->mode + "] WebSocket disconnected", 0, 0, self->config.id, account_display_name(self->config.id));
        push_to_frontend("accounts-updated", accounts_to_json().dump());
    };
    rt->ws->onError = [weak](const std::string& err) {
        auto self = weak.lock();
        if (!self) return;
        add_log("system", account_display_name(self->config.id), "[" + self->mode + "] WebSocket 错误: " + err, 0, 0, self->config.id, account_display_name(self->config.id));
    };
}

static void connect_account_runtime(const std::string& accountId) {
    auto rt = get_account_runtime(accountId);
    if (!rt) return;
    rt->manualStopped = false;
    rt->connecting = true;
    rt->connected = false;
    rt->lastError.clear();
    update_account_runtime_status(accountId, "connecting");
    push_to_frontend("accounts-updated", accounts_to_json().dump());

    std::thread([rt]() {
        std::string accountId = rt->config.id;
        std::string displayName = account_display_name(accountId);
        try {
            std::string mode = rt->mode.empty() ? rt->config.mode : rt->mode;
            std::string host = rt->host.empty() ? rt->config.host : rt->host;
            int port = rt->port > 0 ? rt->port : rt->config.port;
            std::string token = rt->token;
            std::string path = normalize_ws_path(rt->path.empty() ? rt->config.path : rt->path);
            std::string apiHost = rt->apiHost.empty() ? host : rt->apiHost;
            int apiPort = rt->apiPort > 0 ? rt->apiPort : port;
            std::string apiToken = rt->apiToken.empty() ? token : rt->apiToken;
            auto t0 = std::chrono::steady_clock::now();

            if (mode == "forward-ws") {
                setup_runtime_ws_handlers(rt);
                if (rt->ws && rt->ws->isConnected()) rt->ws->disconnect();
                add_log("system", displayName, "连接账号 [" + mode + "] " + host + ":" + std::to_string(port) + path, 0, 0, accountId, displayName);
                if (!rt->ws->connectTo(host, port, path, token)) {
                    rt->connecting = false;
                    rt->connected = false;
                    rt->lastError = "正向 WS 连接失败，请检查端口、路径、Token 和 NapCat/LLBot WS 服务";
                    update_account_runtime_status(accountId, "offline");
                    add_log("error", displayName, "正向 WS 连接失败，请检查账号配置的端口和路径", 0, 0, accountId, displayName);
                    push_to_frontend("accounts-updated", accounts_to_json().dump());
                    return;
                }
                rt->wsMessages = 0;
                rt->wsEvents = 0;
                rt->wsUnknownMessages = 0;
                rt->lastWsPreview.clear();
                std::weak_ptr<AccountRuntime> weakRt = rt;
                std::thread([weakRt]() {
                    std::this_thread::sleep_for(std::chrono::seconds(15));
                    auto self = weakRt.lock();
                    if (!self || !self->ws || !self->ws->isConnected() || self->mode != "forward-ws") return;
                    std::string name = account_display_name(self->config.id);
                    if (self->wsMessages.load() == 0) {
                        add_log("warning", name, "正向 WS 已连接 15 秒，但没有收到任何事件帧；请检查 LLBot/NapCat 是否把事件上报到该 WS 地址", 0, 0, self->config.id, name);
                    } else if (self->wsEvents.load() == 0 && self->wsUnknownMessages.load() > 0) {
                        add_log("warning", name, "正向 WS 收到数据但未识别为 OneBot 事件，最后数据: " + self->lastWsPreview, 0, 0, self->config.id, name);
                    }
                }).detach();
                rt->wsApiAvailable = false;
                json info = callApiViaRuntimeWs(rt, "get_login_info", json::object(), 8000);
                if (!info.contains("data")) {
                    if (path != "/") {
                        add_log("warning", displayName, "正向 WS 已握手但 API 无响应，尝试兼容根路径 /", 0, 0, accountId, displayName);
                        rt->ws->disconnect();
                        setup_runtime_ws_handlers(rt);
                        if (rt->ws->connectTo(host, port, "/", token)) {
                            path = "/";
                            rt->path = "/";
                            rt->wsMessages = 0;
                            rt->wsEvents = 0;
                            rt->wsUnknownMessages = 0;
                            rt->lastWsPreview.clear();
                            info = callApiViaRuntimeWs(rt, "get_login_info", json::object(), 8000);
                            if (info.contains("data")) {
                                rt->wsApiAvailable = true;
                                add_log("system", displayName, "正向 WS 根路径 / 兼容成功，事件和 API 将使用 ws://" + host + ":" + std::to_string(port) + "/", 0, 0, accountId, displayName);
                            }
                        }
                    }
                }
                if (!info.contains("data")) {
                    add_log("warning", displayName, "正向 WS 已握手，但 WS API 暂无响应；将尝试同地址 HTTP API 兜底", 0, 0, accountId, displayName);
                    if (!rt->http) rt->http = std::make_shared<OneBotClient>();
                    rt->http->setConfig(apiHost, apiPort, apiToken);
                    add_log("system", displayName, "尝试 HTTP API 兜底: " + apiHost + ":" + std::to_string(apiPort), 0, 0, accountId, displayName);
                    if (rt->http->connect()) {
                        info = rt->http->getLoginInfo();
                        add_log("system", displayName, "HTTP API 兜底成功，WS 继续用于接收事件", 0, 0, accountId, displayName);
                    } else {
                        rt->connecting = false;
                        rt->connected = true;
                        rt->wsApiAvailable = false;
                        rt->lastError = "事件通道已连接，但 OneBot API 不可用";
                        rt->lastConnectAt = (int64_t)time(nullptr);
                        update_account_runtime_status(accountId, "online");
                        add_log("warning", displayName, "正向 WS 事件通道已连接，但 OneBot API 不可用；群/私聊事件仍会尝试显示，插件 API 调用会失败", 0, 0, accountId, displayName);
                        push_to_frontend("accounts-updated", accounts_to_json().dump());
                        return;
                    }
                } else {
                    rt->wsApiAvailable = true;
                }
                std::string qq, name = displayName.empty() ? "YuexBot" : displayName;
                json d = (info.contains("data") && info["data"].is_object()) ? info["data"] : info;
                if (d.contains("user_id")) qq = json_value_string(d, "user_id");
                if (d.contains("nickname")) name = json_value_string(d, "nickname");
                update_account_runtime_login(accountId, qq, name);
                json fr;
                json gr;
                if (rt->wsApiAvailable.load()) {
                    fr = callApiViaRuntimeWs(rt, "get_friend_list", json::object(), 5000);
                    gr = callApiViaRuntimeWs(rt, "get_group_list", json::object(), 5000);
                } else if (rt->http && rt->http->isConnected()) {
                    fr = rt->http->get_friend_list();
                    gr = rt->http->get_group_list();
                }
                cache_account_lists(accountId, fr, gr);
                auto t1 = std::chrono::steady_clock::now();
                rt->latencyMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                rt->connected = true;
                rt->connecting = false;
                rt->lastError.clear();
                rt->lastConnectAt = (int64_t)time(nullptr);
                rt->reconnectAttempts = 0;
                rt->nextReconnectAt = 0;
                update_account_runtime_status(accountId, "online");
                add_log("system", name, "账号连接成功 QQ " + qq + "，群 " + std::to_string(rt->groups.size()) + "，好友 " + std::to_string(rt->friends.size()), 0, 0, accountId, name);
            } else if (mode == "http-post") {
                if (!rt->http) rt->http = std::make_shared<OneBotClient>();
                rt->http->setConfig(apiHost, apiPort, apiToken);
                add_log("system", displayName, "连接账号 [http-post] " + apiHost + ":" + std::to_string(apiPort), 0, 0, accountId, displayName);
                if (!rt->http->connect()) {
                    rt->connecting = false;
                    rt->connected = false;
                    rt->lastError = "HTTP API 连接失败，请检查地址、端口和 Token";
                    update_account_runtime_status(accountId, "offline");
                    push_to_frontend("accounts-updated", accounts_to_json().dump());
                    return;
                }
                json info = rt->http->getLoginInfo();
                std::string qq, name = displayName.empty() ? "YuexBot" : displayName;
                if (info.contains("user_id")) qq = json_value_string(info, "user_id");
                if (info.contains("nickname")) name = json_value_string(info, "nickname");
                update_account_runtime_login(accountId, qq, name);
                json fr = rt->http->get_friend_list();
                json gr = rt->http->get_group_list();
                cache_account_lists(accountId, fr, gr);
                auto t1 = std::chrono::steady_clock::now();
                rt->latencyMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
                rt->connected = true;
                rt->connecting = false;
                rt->lastError.clear();
                rt->lastConnectAt = (int64_t)time(nullptr);
                rt->reconnectAttempts = 0;
                rt->nextReconnectAt = 0;
                update_account_runtime_status(accountId, "online");
                add_log("system", name, "账号连接成功 QQ " + qq + "，群 " + std::to_string(rt->groups.size()) + "，好友 " + std::to_string(rt->friends.size()), 0, 0, accountId, name);
            } else {
                add_log("system", displayName, "反向 WS 使用框架统一监听端口，已切换到传统监听流程", 0, 0, accountId, displayName);
                rt->connecting = false;
            }
            push_to_frontend("accounts-updated", accounts_to_json().dump());
        } catch (const std::exception& e) {
            rt->connecting = false;
            rt->connected = false;
            rt->lastError = normalize_external_text(e.what());
            update_account_runtime_status(accountId, "offline");
            add_log("error", displayName, std::string("账号连接异常: ") + e.what(), 0, 0, accountId, displayName);
            push_to_frontend("accounts-updated", accounts_to_json().dump());
        } catch (...) {
            rt->connecting = false;
            rt->connected = false;
            rt->lastError = "未知连接异常";
            update_account_runtime_status(accountId, "offline");
            add_log("error", displayName, "账号连接异常: unknown", 0, 0, accountId, displayName);
            push_to_frontend("accounts-updated", accounts_to_json().dump());
        }
    }).detach();
}

static void stop_reverse_ws_listener() {
    g_reverseWsListening = false;
    std::lock_guard<std::mutex> lock(g_reverseWsMutex);
    if (g_reverseWsSocket != INVALID_SOCKET) {
        closesocket(g_reverseWsSocket);
        g_reverseWsSocket = INVALID_SOCKET;
    }
    g_reverseWsPort = 0;
    g_reverseWsHost.clear();
    g_reverseWsToken.clear();
}

static void start_reverse_ws_listener(const std::string& host, int port, const std::string& token) {
    {
        std::lock_guard<std::mutex> lock(g_reverseWsMutex);
        if (g_reverseWsListening && g_reverseWsPort == port && g_reverseWsHost == host && g_reverseWsToken == token) {
            add_log("system", "", "反向 WS 已在监听: " + host + ":" + std::to_string(port));
            return;
        }
    }

    stop_reverse_ws_listener();
    g_reverseWsListening = true;
    g_reverseWsPort = port;
    g_reverseWsHost = host;
    g_reverseWsToken = token;

    std::thread([host, port, token]() {
        SOCKET sfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sfd == INVALID_SOCKET) {
            g_reverseWsListening = false;
            add_log("system", "", "反向 WS 创建监听 Socket 失败");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(g_reverseWsMutex);
            g_reverseWsSocket = sfd;
        }
        int opt = 1;
        setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

        struct sockaddr_in saddr = {};
        saddr.sin_family = AF_INET;
        if (host.empty() || host == "0.0.0.0" || host == "*") {
            saddr.sin_addr.s_addr = INADDR_ANY;
        } else if (inet_pton(AF_INET, host.c_str(), &saddr.sin_addr) != 1) {
            add_log("system", "", "反向 WS 监听地址无效，已回退到 0.0.0.0");
            saddr.sin_addr.s_addr = INADDR_ANY;
        }
        saddr.sin_port = htons(port);

        if (::bind(sfd, (struct sockaddr*)&saddr, sizeof(saddr)) == SOCKET_ERROR) {
            int err = WSAGetLastError();
            add_log("system", "", "反向 WS 绑定端口失败: " + host + ":" + std::to_string(port) + " error=" + std::to_string(err));
            {
                std::lock_guard<std::mutex> lock(g_reverseWsMutex);
                if (g_reverseWsSocket == sfd) g_reverseWsSocket = INVALID_SOCKET;
            }
            closesocket(sfd);
            g_reverseWsListening = false;
            json status; status["connected"] = false; status["error"] = "reverse ws listen failed";
            push_to_frontend("status-changed", status.dump());
            return;
        }

        if (listen(sfd, 8) == SOCKET_ERROR) {
            add_log("system", "", "reverse WS listen failed");
            {
                std::lock_guard<std::mutex> lock(g_reverseWsMutex);
                if (g_reverseWsSocket == sfd) g_reverseWsSocket = INVALID_SOCKET;
            }
            closesocket(sfd);
            g_reverseWsListening = false;
            return;
        }

        add_log("system", "", "反向 WS 开始监听: " + host + ":" + std::to_string(port));
        while (g_reverseWsListening && g_connMode == "reverse-ws") {
            struct sockaddr_in caddr = {};
            int clen = sizeof(caddr);
            SOCKET cs = accept(sfd, (struct sockaddr*)&caddr, &clen);
            if (cs == INVALID_SOCKET) {
                if (g_reverseWsListening) add_log("system", "", "reverse WS accept failed");
                break;
            }

            char ipbuf[64] = {0};
            inet_ntop(AF_INET, &caddr.sin_addr, ipbuf, sizeof(ipbuf));
            add_log("system", "", std::string("反向 WS 客户端接入: ") + ipbuf);

            if (g_ws.isConnected()) g_ws.disconnect();
            setup_ws_handlers();
            g_ws.onConnect = []() {
                add_log("system", "", "reverse WS client connected");
                g_connected = true;
                g_autoReconnect = false;
                std::thread([]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    publish_connection_info("reverse-ws", g_host, g_port);
                }).detach();
            };
            g_ws.onDisconnect = []() {
                add_log("system", "", "reverse WS handshake failed: missing upgrade headers");
                g_connected = false;
                json st; st["connected"] = false; st["mode"] = "reverse-ws";
                push_to_frontend("status-changed", st.dump());
            };
            if (g_ws.acceptFrom(cs, token)) {
                add_log("system", "", "reverse WS handshake accepted");
            } else {
                add_log("system", "", "reverse WS handshake failed");
            }
        }

        {
            std::lock_guard<std::mutex> lock(g_reverseWsMutex);
            if (g_reverseWsSocket == sfd) g_reverseWsSocket = INVALID_SOCKET;
        }
        closesocket(sfd);
        g_reverseWsListening = false;
        add_log("system", "", "reverse WS listener stopped");
    }).detach();
}

static void do_connect(const std::string& host, int port, const std::string& token, const std::string& mode = "", const std::string& wsPath = "") {
    g_host = host;
    g_port = port;
    g_token = token;
    if (!mode.empty()) g_connMode = mode;
    if (!wsPath.empty()) g_wsPath = normalize_ws_path(wsPath);
    g_onebot.setConfig(host, port, token);
    // Auto-save connection settings
    {
        json cfg;
        cfg["host"] = host; cfg["port"] = port; cfg["token"] = token; cfg["mode"] = g_connMode;
        cfg["path"] = g_wsPath;
        save_config_file("config.json", cfg);
    }
    if (yaml_set) {
        yaml_set("config.yaml", "connection.host", host.c_str());
        yaml_set("config.yaml", "connection.port", std::to_string(port).c_str());
        yaml_set("config.yaml", "connection.token", token.c_str());
    }
    add_log("system", "", "开始连接 [" + g_connMode + "] " + host + ":" + std::to_string(port));

    // Disconnect existing connections
    if (g_connMode != "reverse-ws") stop_reverse_ws_listener();
    if (g_ws.isConnected()) g_ws.disconnect();
    g_onebot.disconnect();

    std::thread([host, port, token, wsPath, modeSnapshot = g_connMode]() {
        try {
        startup_trace("connect thread begin mode=%s host=%s port=%d", modeSnapshot.c_str(), host.c_str(), port);
        // === Forward WS mode ===
        if (modeSnapshot == "forward-ws") {
            setup_ws_handlers();
            std::string path = normalize_ws_path(wsPath.empty() ? g_wsPath : wsPath);
            add_log("system", "", "正在连接正向 WS: " + host + ":" + std::to_string(port) + path);
            bool wsOk = g_ws.connectTo(host, port, path, token);
            startup_trace("forward ws connect result=%d", wsOk ? 1 : 0);
            if (!g_ws.isConnected()) {
                add_log("system", "", "forward WS failed, trying HTTP POST mode");
                // Fallback: try HTTP API
                g_onebot.setConfig(host, port, token);
                if (g_onebot.connect()) {
                    g_connected = true;
                    g_onebot.startPolling();
                    // Load data and notify
                    auto info = g_onebot.getLoginInfo();
                    std::string qq, name = "YuexBot";
                    if (info.contains("data")) {
                        auto d = info["data"];
                        if (d.contains("user_id")) qq = json_value_string(d, "user_id");
                        if (d.contains("nickname")) name = json_value_string(d, "nickname");
                    }
                    add_log("system", "", "HTTP API 连接成功，账号 QQ " + qq + " (" + name + ")");
                    {
                        std::lock_guard<std::mutex> lock(g_mutex);
                        std::string accountId = resolve_account_id_for_login(qq, host, port);
                        if (!accountId.empty()) g_activeAccountId = accountId;
                        for (auto& a : g_accounts) {
                            if (a.id == g_activeAccountId) { a.name = name; a.qq = qq; break; }
                        }
                        sync_account_runtimes_from_configs();
                        save_accounts_to_yaml();
                    }
                    update_account_runtime_login(g_activeAccountId, qq, name);
                    update_account_runtime_status(g_activeAccountId, "online");
                    auto fr = g_onebot.get_friend_list();
                    auto gr = g_onebot.get_group_list();
                    cache_account_lists(g_activeAccountId, fr, gr);
                    json status; status["connected"] = true; status["qq"] = qq; status["name"] = name;
                    status["friends"] = (int)g_friends.size(); status["groups"] = (int)g_groups.size();
                    push_to_frontend("status-changed", status.dump());
                    push_to_frontend("accounts-updated", accounts_to_json().dump());
                    return;
                }
                json status; status["connected"] = false; status["error"] = "all connection methods failed";
                g_autoReconnect = false;
                push_to_frontend("status-changed", status.dump());
                return;
            }
            json probe = callApiViaWs("get_login_info", json::object(), 3000);
            if (!probe.contains("data") && path != "/") {
                add_log("warning", "", "正向 WS 握手成功但 OneBot API 无响应，尝试 LLBot 根路径 /");
                startup_trace("forward ws probe failed, trying root path");
                g_ws.disconnect();
                setup_ws_handlers();
                if (g_ws.connectTo(host, port, "/", token)) {
                    json rootProbe = callApiViaWs("get_login_info", json::object(), 3000);
                    if (rootProbe.contains("data")) {
                        path = "/";
                        g_wsPath = "/";
                        probe = rootProbe;
                        add_log("system", "", "LLBot 正向 WS 根路径 / 探测成功");
                        startup_trace("forward ws root path probe ok");
                    } else {
                        startup_trace("forward ws root path probe timeout");
                    }
                }
            }
            if (!probe.contains("data")) {
                g_connected = false;
                add_log("error", "", "正向 WS 只完成握手，但 OneBot API/事件通道无响应；请检查 LLBot 正向 WS 端口和路径，LLBot 正向 WS 通常使用 ws://host:port/");
                json status; status["connected"] = false;
                status["error"] = "WS handshake ok but OneBot channel not ready";
                g_autoReconnect = false;
                push_to_frontend("status-changed", status.dump());
                return;
            }
            add_log("system", "", "正向 WS 已连接: " + host + ":" + std::to_string(port) + path);
            g_connected = true;  // Mark connected immediately
            std::thread([host, port]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                publish_connection_info("forward-ws", host, port);
            }).detach();
        }
        // === Reverse WS mode ===
        else if (modeSnapshot == "reverse-ws") {
            add_log("system", "", "准备启动反向 WS 监听: " + host + ":" + std::to_string(port));
            start_reverse_ws_listener(host, port, token);
            return;
        }
        // === HTTP POST mode ===
        else {
            g_onebot.setConfig(host, port, token);
            if (!g_onebot.connect()) {
                add_log("system", "", "HTTP API 连接失败: " + host + ":" + std::to_string(port));
                json status; status["connected"] = false; status["error"] = "HTTP API connection failed";
                g_autoReconnect = false;
                push_to_frontend("status-changed", status.dump());
                return;
            }
        }

        // Common post-connection logic
        auto t0 = std::chrono::steady_clock::now();
        bool ok = ((modeSnapshot == "forward-ws" || modeSnapshot == "reverse-ws") && g_ws.isConnected()) ||
                  (modeSnapshot == "http-post" && g_onebot.isConnected());

        auto t1 = std::chrono::steady_clock::now();
        g_latencyMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        if (ok && modeSnapshot != "forward-ws" && modeSnapshot != "reverse-ws") {
            g_connected = true;
            json info;
            if ((modeSnapshot == "forward-ws" || modeSnapshot == "reverse-ws") && g_ws.isConnected()) {
                info = callApiViaWs("get_login_info");
            } else {
                info = g_onebot.getLoginInfo();
            }
            std::string qq, name = "YuexBot";
            if (info.contains("data")) {
                auto d = info["data"];
                if (d.contains("user_id")) qq = json_value_string(d, "user_id");
                if (d.contains("nickname")) name = json_value_string(d, "nickname");
            }
            add_log("system", "", "连接成功，延迟 " + std::to_string(g_latencyMs.load()) + "ms");
            g_onebot.startPolling();
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                std::string accountId = resolve_account_id_for_login(qq, g_host, g_port);
                if (!accountId.empty()) g_activeAccountId = accountId;
            }

            // Load friends
            json fr;
            if ((modeSnapshot == "forward-ws" || modeSnapshot == "reverse-ws") && g_ws.isConnected()) {
                fr = callApiViaWs("get_friend_list");
            } else {
                fr = g_onebot.get_friend_list();
            }
            // Load groups
            json gr;
            if ((modeSnapshot == "forward-ws" || modeSnapshot == "reverse-ws") && g_ws.isConnected()) {
                gr = callApiViaWs("get_group_list");
            } else {
                gr = g_onebot.get_group_list();
            }
            cache_account_lists(g_activeAccountId, fr, gr);
            add_log("system", "", "loaded " + std::to_string(g_friends.size()) + " friends");
            add_log("system", "", "loaded " + std::to_string(g_groups.size()) + " groups");

            // Update account list with connected account
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                bool found = false;
                for (auto& a : g_accounts) {
                    if (a.id == g_activeAccountId || a.qq == qq || (a.host == g_host && a.port == g_port)) {
                        a.qq = qq; a.name = name; a.status = "online";
                        found = true; break;
                    }
                }
                if (!found && !qq.empty()) {
                    AccountConfig a;
                    a.id = "acc_" + qq;
                    a.name = name; a.qq = qq;
                    a.host = g_host; a.port = g_port; a.token = g_token;
                    a.mode = "reverse-ws"; a.status = "online";
                    g_accounts.push_back(a);
                }
                sync_account_runtimes_from_configs();
                save_accounts_to_yaml();
            }
            update_account_runtime_login(g_activeAccountId, qq, name);
            update_account_runtime_status(g_activeAccountId, "online");

            json status;
            status["connected"] = true;
            status["qq"] = qq;
            status["name"] = name;
            status["friends"] = (int)g_friends.size();
            status["groups"] = (int)g_groups.size();
            push_to_frontend("status-changed", status.dump());
            push_to_frontend("accounts-updated", accounts_to_json().dump());
        } else {
            add_log("system", "", "connection failed, check OneBot server");
            json status;
            status["connected"] = false;
            status["error"] = "connection failed";
            g_autoReconnect = false;
            push_to_frontend("status-changed", status.dump());
        }
        startup_trace("connect thread end mode=%s", modeSnapshot.c_str());
        } catch (const std::exception& e) {
            startup_trace("connect thread exception=%s", e.what());
            add_log("system", "", std::string("运行异常: ") + e.what());
            g_connected = false;
            g_autoReconnect = false;
            json status;
            status["connected"] = false;
            status["error"] = std::string("运行异常: ") + e.what();
            push_to_frontend("status-changed", status.dump());
        } catch (...) {
            startup_trace("connect thread unknown exception");
            add_log("system", "", "连接线程异常: unknown");
            g_connected = false;
            g_autoReconnect = false;
            json status; status["connected"] = false; status["error"] = "connection failed";
            push_to_frontend("status-changed", status.dump());
        }
    }).detach();
}

static void do_disconnect() {
    g_autoReconnect = false;
    stop_reverse_ws_listener();
    g_ws.disconnect();
    g_onebot.disconnect();
    g_connected = false;
    update_account_runtime_status(g_activeAccountId, "offline");
    add_log("system", "", "all connections closed");
    json status;
    status["connected"] = false;
    push_to_frontend("status-changed", status.dump());
}
// ============================================================
// Background Threads
// ============================================================
static void poll_thread() {
    int reconnectAttempts = 0;
    while (!g_shuttingDown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (g_shuttingDown.load()) break;

        if (g_connMode == "reverse-ws" && g_reverseWsListening && !g_connected) {
            reconnectAttempts = 0;
            continue;
        }

        // Check WS connection health (for WS modes)
        if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_connected && !g_ws.isConnected()) {
            g_connected = false;
            g_autoReconnect = (g_connMode == "forward-ws");
            add_log("system", "", "WebSocket 已断开");
            json status; status["connected"] = false; status["error"] = "WS disconnected";
            push_to_frontend("status-changed", status.dump());
        }

        if (!g_connected) {
            if (g_autoReconnect && !g_host.empty()) {
                reconnectAttempts++;
                if (reconnectAttempts >= 5) {
                    int attempt = reconnectAttempts / 5;
                    add_log("system", "", "auto reconnecting, attempt " + std::to_string(attempt));
                    do_connect(g_host, g_port, g_token, g_connMode, g_wsPath);
                    std::this_thread::sleep_for(std::chrono::seconds(5));
                    if (g_connected) {
                        reconnectAttempts = 0;
                        g_autoReconnect = false;
                    } else if (attempt >= 3) {
                        // After 3 retries, stop trying
                        g_autoReconnect = false;
                        add_log("system", "", "auto reconnect failed, stopped");
                        json status; status["connected"] = false; status["error"] = "reconnect failed";
                        push_to_frontend("status-changed", status.dump());
                    }
                }
            }
            continue;
        }
        reconnectAttempts = 0;

        // Poll events and health check (only for non-WS modes)
        if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_ws.isConnected()) {
            // WS modes: receive thread handles events and disconnection.
        } else if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && !g_ws.isConnected()) {
            // WS lost - handled by callbacks / listener.
        } else {
            // HTTP-based modes: poll events and check health
            g_onebot.pollEvents();
            if (!g_onebot.isConnected()) {
                g_connected = false;
                g_autoReconnect = true;
                add_log("system", "", "HTTP 连接已断开，准备自动重连");
                json status; status["connected"] = false; status["error"] = "connection lost";
                push_to_frontend("status-changed", status.dump());
            }
        }
        g_totalEvents = g_onebot.getEventCount();
    }
}

static void stats_thread() {
    FILETIME prev_idle, prev_kernel, prev_user;
    GetSystemTimes(&prev_idle, &prev_kernel, &prev_user);
    bool first = true;

    while (!g_shuttingDown.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        if (g_shuttingDown.load()) break;
        if (g_windowId == 0) continue;

        // --- CPU: use GetSystemTimes for accurate system-wide + process CPU ---
        FILETIME idle, kernel, user;
        GetSystemTimes(&idle, &kernel, &user);
        ULARGE_INTEGER k, u, i, pk, pu, pi;
        k.LowPart = kernel.dwLowDateTime; k.HighPart = kernel.dwHighDateTime;
        u.LowPart = user.dwLowDateTime; u.HighPart = user.dwHighDateTime;
        i.LowPart = idle.dwLowDateTime; i.HighPart = idle.dwHighDateTime;
        pk.LowPart = prev_kernel.dwLowDateTime; pk.HighPart = prev_kernel.dwHighDateTime;
        pu.LowPart = prev_user.dwLowDateTime; pu.HighPart = prev_user.dwHighDateTime;
        pi.LowPart = prev_idle.dwLowDateTime; pi.HighPart = prev_idle.dwHighDateTime;

        float cpu = 0;
        if (!first) {
            ULONGLONG sys = (k.QuadPart - pk.QuadPart) + (u.QuadPart - pu.QuadPart);
            ULONGLONG idle_time = i.QuadPart - pi.QuadPart;
            if (sys > 0) cpu = (float)(sys - idle_time) / (float)sys * 100.0f;
        }
        prev_idle = idle; prev_kernel = kernel; prev_user = user;
        first = false;

        // --- Memory ---
        float memMB = process_working_set_mb();

        // --- Latency: measure API response time ---
        if (g_connected && !g_wsApiCallMutex.try_lock()) {
            // Another API call is already in flight; avoid concurrent WS API requests.
        } else if (g_connected) {
            std::unique_lock<std::mutex> apiProbeLock(g_wsApiCallMutex, std::adopt_lock);
            auto t0 = std::chrono::steady_clock::now();
            if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_ws.isConnected()) {
                startup_trace("stats latency ws begin");
                int echo = ++g_wsEcho;
                std::string echoStr = std::to_string(echo);
                json req;
                req["action"] = "get_status";
                req["params"] = json::object();
                req["echo"] = echoStr;
                if (g_ws.sendText(req.dump())) {
                    auto start = std::chrono::steady_clock::now();
                    while (true) {
                        {
                            std::lock_guard<std::mutex> lock(g_wsResponseMutex);
                            auto it = g_wsResponses.find(echoStr);
                            if (it != g_wsResponses.end()) {
                                g_wsResponses.erase(it);
                                break;
                            }
                        }
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start).count();
                        if (elapsed > 1000) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                }
                startup_trace("stats latency ws end");
            } else {
                g_onebot.get_status();
            }
            auto t1 = std::chrono::steady_clock::now();
            g_latencyMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        }

        json stats;
        stats["cpu"] = (int)cpu;
        stats["memory"] = (int)memMB;
        stats["latency"] = g_latencyMs.load();
        stats["uptime"] = uptime_str();
        stats["messages"] = g_totalMessages.load();
        stats["friends"] = (int)g_friends.size();
        stats["groups"] = (int)g_groups.size();
        stats["events"] = g_totalEvents.load();
        stats["event_port"] = g_eventServerPort;
        push_to_frontend("system-stats", stats.dump());
    }
}
// ============================================================
// IPC Handlers (Frontend -> Backend)
// ============================================================

static const char* ipc_connect(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string host = j.value("host", "127.0.0.1");
        int port = j.value("port", 3001);
        std::string token = j.value("token", "");
        g_autoReconnect = true;
        std::string mode = j.value("mode", g_connMode);
        std::string path = json_get_ws_path(j, g_wsPath);
        do_connect(host, port, token, mode, path);
    } catch (...) {}
    return jade_text_create("{\"ok\":true}");
}

static const char* ipc_disconnect(uint32_t wid, const char* data) {
    do_disconnect();
    return jade_text_create("{\"ok\":true}");
}

static const char* ipc_quit(uint32_t wid, const char* data) {
    (void)wid;
    (void)data;
    std::thread([] {
        request_process_exit("ipc quit");
    }).detach();
    return jade_text_create("{\"ok\":true}");
}

static const char* ipc_get_status(uint32_t wid, const char* data) {
    json r;
    r["connected"] = g_connected.load();
    r["uptime"] = uptime_str();
    r["messages"] = g_totalMessages.load();
    r["friends"] = (int)g_friends.size();
    r["groups"] = (int)g_groups.size();
    r["events"] = g_totalEvents.load();
    r["latency"] = g_latencyMs.load();
    r["host"] = g_host;
    r["port"] = g_port;
    r["path"] = g_wsPath;
    r["mode"] = g_connMode;
    r["account_id"] = g_activeAccountId;
    r["event_port"] = g_eventServerPort;
    for (auto& a : g_accounts) {
        if (a.id == g_activeAccountId) {
            r["qq"] = a.qq;
            r["name"] = a.name;
            r["account_status"] = a.status;
            break;
        }
    }
    if (g_connected) {
        json info;
        if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_ws.isConnected()) {
            info = callApiViaWs("get_login_info", json::object(), 3000);
        } else {
            info = g_onebot.getLoginInfo();
        }
        if (info.contains("data")) {
            auto d = info["data"];
            if (d.contains("user_id")) {
                r["qq"] = json_value_string(d, "user_id");
            }
            if (d.contains("nickname")) r["name"] = json_value_string(d, "nickname");
        }
    }
    return jade_text_create(r.dump().c_str());
}

static const char* ipc_get_friends(uint32_t wid, const char* data) {
    json r = get_account_list_payload("friends", data);
    return jade_text_create(r.dump().c_str());
}

static const char* ipc_get_groups(uint32_t wid, const char* data) {
    json r = get_account_list_payload("groups", data);
    return jade_text_create(r.dump().c_str());
}

static const char* ipc_get_logs(uint32_t wid, const char* data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    json arr = json::array();
    for (auto& e : g_logs) {
        arr.push_back(log_entry_to_json(e));
    }
    return jade_text_create(arr.dump().c_str());
}

static const char* ipc_clear_logs(uint32_t wid, const char* data) {
    { std::lock_guard<std::mutex> lock(g_mutex); g_logs.clear(); }
    g_totalMessages = 0;
    push_to_frontend("logs-cleared", "{}");
    return jade_text_create("{\"ok\":true}");
}

static const char* ipc_compact_memory(uint32_t wid, const char* data) {
    float before = process_working_set_mb();
    {
        std::lock_guard<std::mutex> lock(g_wsResponseMutex);
        g_wsResponses.clear();
    }
    {
        std::lock_guard<std::mutex> lock(g_accountRuntimeMutex);
        for (auto& kv : g_accountRuntimes) {
            if (!kv.second) continue;
            std::lock_guard<std::mutex> wsLock(kv.second->wsResponseMutex);
            kv.second->wsResponses.clear();
        }
    }
    trim_working_set_now();
    float after = process_working_set_mb();
    json r;
    r["ok"] = true;
    r["before_mb"] = (int)before;
    r["after_mb"] = (int)after;
    add_log("system", "", "已执行内存优化: " + std::to_string((int)before) + "MB -> " + std::to_string((int)after) + "MB");
    return jade_text_create(r.dump().c_str());
}

// FIXED: was using "target" but frontend sends "target_id"
static const char* ipc_send_message(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        int64_t target_id = j.value("target_id", (int64_t)0);
        if (target_id == 0) target_id = j.value("target", (int64_t)0); // fallback
        std::string msg = j.value("message", "");
        int mode = j.value("mode", 0);
        std::string accountId = j.value("account_id", j.value("accountId", ""));
        if (target_id <= 0 || msg.empty()) {
            return jade_text_create("{\"ok\":false,\"error\":\"消息目标或内容为空\"}");
        }
        json r;
        bool ok = false;
        if (!accountId.empty()) {
            json p;
            if (mode == 0) {
                p["user_id"] = target_id;
                p["message"] = msg;
                r = call_onebot_api_bridge_for_account(accountId, "send_private_msg", p);
            } else {
                p["group_id"] = target_id;
                p["message"] = msg;
                r = call_onebot_api_bridge_for_account(accountId, "send_group_msg", p);
            }
            ok = plugin_retcode_ok(r) != 0;
            std::string label = (mode == 0 ? "私聊" : "群聊");
            add_log(ok ? "system" : "warning", account_display_name(accountId), label + " -> " + std::to_string(target_id) + (ok ? " 发送成功" : " 发送失败"), 0, mode == 0 ? 0 : target_id, accountId, account_display_name(accountId));
        } else if (g_connected) {
            if (mode == 0) {
                if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_ws.isConnected()) {
                    json p; p["user_id"] = target_id; p["message"] = msg;
                    r = callApiViaWs("send_private_msg", p);
                } else {
                    r = g_onebot.send_private_msg(target_id, msg);
                }
                add_log("system", "私聊 -> " + std::to_string(target_id), msg);
            } else {
                if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_ws.isConnected()) {
                    json p; p["group_id"] = target_id; p["message"] = msg;
                    r = callApiViaWs("send_group_msg", p);
                } else {
                    r = g_onebot.send_group_msg(target_id, msg);
                }
                add_log("system", "群聊 -> " + std::to_string(target_id), msg);
            }
            ok = plugin_retcode_ok(r) != 0;
        }
        json out;
        out["ok"] = ok;
        out["response"] = r;
        if (!ok) out["error"] = "发送失败，请检查该账号 API 通道是否可用";
        return jade_text_create(out.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"发送消息异常\"}");
}

static bool parse_service_endpoint(std::string url, std::string& host, int& port) {
    host.clear();
    port = 80;
    auto scheme = url.find("://");
    if (scheme != std::string::npos) {
        std::string proto = url.substr(0, scheme);
        port = (proto == "https") ? 443 : 80;
        url = url.substr(scheme + 3);
    }
    auto slash = url.find('/');
    if (slash != std::string::npos) url = url.substr(0, slash);
    if (url.empty()) return false;
    if (url.size() >= 2 && url.front() == '[') {
        auto close = url.find(']');
        if (close == std::string::npos) return false;
        host = url.substr(1, close - 1);
        if (close + 1 < url.size() && url[close + 1] == ':') {
            try { port = std::stoi(url.substr(close + 2)); } catch (...) { return false; }
        }
    } else {
        auto colon = url.rfind(':');
        if (colon != std::string::npos && url.find(':') == colon) {
            host = url.substr(0, colon);
            try { port = std::stoi(url.substr(colon + 1)); } catch (...) { return false; }
        } else {
            host = url;
        }
    }
    return !host.empty() && port > 0 && port <= 65535;
}

static bool tcp_probe_endpoint(const std::string& host, int port, int timeoutMs, std::string& error) {
    addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    std::string portText = std::to_string(port);
    int gai = getaddrinfo(host.c_str(), portText.c_str(), &hints, &result);
    if (gai != 0 || !result) {
        error = "服务端地址解析失败";
        return false;
    }
    bool ok = false;
    int lastErr = 0;
    for (addrinfo* rp = result; rp; rp = rp->ai_next) {
        SOCKET s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        u_long nonblock = 1;
        ioctlsocket(s, FIONBIO, &nonblock);
        int cr = connect(s, rp->ai_addr, (int)rp->ai_addrlen);
        if (cr == 0) {
            ok = true;
        } else {
            lastErr = WSAGetLastError();
            if (lastErr == WSAEWOULDBLOCK || lastErr == WSAEINPROGRESS || lastErr == WSAEINVAL) {
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(s, &wfds);
                timeval tv;
                tv.tv_sec = timeoutMs / 1000;
                tv.tv_usec = (timeoutMs % 1000) * 1000;
                int sr = select(0, nullptr, &wfds, nullptr, &tv);
                if (sr > 0 && FD_ISSET(s, &wfds)) {
                    int soerr = 0;
                    int len = sizeof(soerr);
                    getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&soerr, &len);
                    ok = (soerr == 0);
                    lastErr = soerr;
                }
            }
        }
        closesocket(s);
        if (ok) break;
    }
    freeaddrinfo(result);
    if (!ok) error = "服务端不可连接，错误码 " + std::to_string(lastErr);
    return ok;
}

static const char* ipc_check_service(uint32_t wid, const char* data) {
    (void)wid;
    try {
        json req = json::parse(data ? data : "{}");
        bool enabled = req.value("enabled", false);
        std::string url = req.value("url", "");
        if (!enabled) return jade_text_create("{\"ok\":false,\"error\":\"服务端同步未启用\"}");
        std::string host;
        int port = 0;
        if (!parse_service_endpoint(url, host, port)) {
            return jade_text_create("{\"ok\":false,\"error\":\"服务端地址格式不正确\"}");
        }
        std::string err;
        bool ok = tcp_probe_endpoint(host, port, 1500, err);
        json r;
        r["ok"] = ok;
        r["host"] = host;
        r["port"] = port;
        r["framework_version"] = kYuexBotVersion;
        r["accounts"] = g_accounts.size();
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            r["plugins"] = g_plugins.size();
        }
        if (!ok) r["error"] = err.empty() ? "服务端不可连接" : err;
        return jade_text_create(r.dump().c_str());
    } catch (...) {
        return jade_text_create("{\"ok\":false,\"error\":\"服务端测试异常\"}");
    }
}

static const char* ipc_save_settings(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        save_config_file("settings.json", j);
        // Also save legacy config for backward compatibility
        if (j.contains("connections")) {
            auto& c = j["connections"];
            if (c.contains("reverseWs") && c["reverseWs"].contains("port")) {
                auto& rw = c["reverseWs"];
                if (yaml_set) {
                    yaml_set("config.yaml", "connection.host", rw.value("host", "0.0.0.0").c_str());
                    yaml_set("config.yaml", "connection.port", std::to_string(rw.value("port", 3001)).c_str());
                    yaml_set("config.yaml", "connection.token", rw.value("token", "").c_str());
                }
            }
        }
        add_log("system", "", "settings saved");
    } catch (...) {}
    return jade_text_create("{\"ok\":true}");
}

static const char* ipc_load_settings(uint32_t wid, const char* data) {
    // Try new format first
    auto settings = load_config_file("settings.json");
    if (settings.contains("connections")) {
        return jade_text_create(settings.dump().c_str());
    }
    // Fallback to legacy format
    json r;
    r["host"] = "127.0.0.1"; r["port"] = 3001;
    r["token"] = ""; r["mode"] = "reverse-ws"; r["path"] = "/onebot/v11/ws";
    if (yaml_get) {
        char buf[512];
        if (yaml_get("config.yaml", "connection.host", buf, sizeof(buf)) > 0) r["host"] = strip_quotes(buf);
        if (yaml_get("config.yaml", "connection.port", buf, sizeof(buf)) > 0) { try { r["port"] = std::stoi(buf); } catch (...) {} }
        if (yaml_get("config.yaml", "connection.token", buf, sizeof(buf)) > 0) r["token"] = strip_quotes(buf);
        if (yaml_get("config.yaml", "connection.mode", buf, sizeof(buf)) > 0) r["mode"] = strip_quotes(buf);
    }
    return jade_text_create(r.dump().c_str());
}

static const char* ipc_get_group_members(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        int64_t gid = j.value("group_id", (int64_t)0);
        std::string accountId = j.value("account_id", j.value("accountId", ""));
        if (!accountId.empty() && gid > 0) {
            json p; p["group_id"] = gid;
            auto r = call_onebot_api_bridge_for_account(accountId, "get_group_member_list", p);
            return jade_text_create(r.dump().c_str());
        }
        if (g_connected && gid > 0) {
            json r;
            if ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_ws.isConnected()) {
                json p; p["group_id"] = gid;
                r = callApiViaWs("get_group_member_list", p);
            } else {
                r = g_onebot.get_group_member_list(gid);
            }
            return jade_text_create(r.dump().c_str());
        }
    } catch (...) {}
    return jade_text_create("{\"data\":[]}");
}

static const char* ipc_get_group_info(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        int64_t gid = j.value("group_id", (int64_t)0);
        std::string accountId = j.value("account_id", j.value("accountId", ""));
        if (!accountId.empty() && gid > 0) {
            json p; p["group_id"] = gid;
            auto r = call_onebot_api_bridge_for_account(accountId, "get_group_info", p);
            return jade_text_create(r.dump().c_str());
        }
        if (g_connected && gid > 0) {
            auto r = g_onebot.get_group_info(gid);
            return jade_text_create(r.dump().c_str());
        }
    } catch (...) {}
    return jade_text_create("{}");
}

static const char* ipc_group_action(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string accountId = j.value("account_id", j.value("accountId", ""));
        std::string action = j.value("action", "");
        json params = j.value("params", json::object());
        static const std::set<std::string> allowed = {
            "set_group_ban",
            "set_group_kick",
            "set_group_admin",
            "set_group_name",
            "set_group_card",
            "set_group_whole_ban",
            "set_group_leave",
            "set_group_special_title"
        };
        if (allowed.find(action) == allowed.end()) {
            return jade_text_create("{\"ok\":false,\"error\":\"不支持的群操作\"}");
        }
        if (!params.is_object()) params = json::object();
        json r = !accountId.empty()
            ? call_onebot_api_bridge_for_account(accountId, action, params)
            : call_onebot_api_bridge(action, params);
        bool ok = plugin_retcode_ok(r) != 0;
        json out;
        out["ok"] = ok;
        out["response"] = r;
        if (!ok) {
            std::string msg = r.value("wording", r.value("msg", "群操作失败，请检查账号权限或 API 通道"));
            out["error"] = msg;
        }
        int64_t gid = json_value_i64(params, "group_id", 0);
        std::string who = accountId.empty() ? "" : account_display_name(accountId);
        add_log(ok ? "system" : "warning", who, std::string("群操作 ") + action + (ok ? " 成功" : " 失败"), 0, gid, accountId, who);
        return jade_text_create(out.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"群操作异常\"}");
}

static const char* ipc_group_query(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string accountId = j.value("account_id", j.value("accountId", ""));
        std::string action = j.value("action", "");
        json params = j.value("params", json::object());
        static const std::set<std::string> allowed = {
            "get_group_info",
            "get_group_member_list",
            "get_group_root_files",
            "get_group_files",
            "_get_group_notice",
            "get_group_essence_msg_list"
        };
        if (allowed.find(action) == allowed.end()) {
            return jade_text_create("{\"ok\":false,\"error\":\"不支持的群查询\"}");
        }
        if (!params.is_object()) params = json::object();
        json r = !accountId.empty()
            ? call_onebot_api_bridge_for_account(accountId, action, params)
            : call_onebot_api_bridge(action, params);
        bool ok = plugin_retcode_ok(r) != 0 || (r.is_object() && r.contains("data") && !r.contains("retcode"));
        json out;
        out["ok"] = ok;
        out["data"] = r.contains("data") ? r["data"] : r;
        out["response"] = r;
        if (!ok) {
            out["error"] = r.value("wording", r.value("msg", "群查询失败，请检查账号 API 通道"));
        }
        return jade_text_create(out.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"群查询异常\"}");
}

static const char* ipc_request_action(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string accountId = j.value("account_id", j.value("accountId", ""));
        std::string requestType = j.value("request_type", j.value("requestType", ""));
        std::string subType = j.value("sub_type", j.value("subType", ""));
        std::string flag = j.value("flag", "");
        bool approve = j.value("approve", false);
        std::string reason = normalize_external_text(j.value("reason", ""));
        if (flag.empty()) {
            return jade_text_create("{\"ok\":false,\"error\":\"请求 flag 为空，无法处理\"}");
        }
        json params;
        params["flag"] = flag;
        params["approve"] = approve;
        std::string action;
        if (requestType == "friend") {
            action = "set_friend_add_request";
            if (!reason.empty()) params["remark"] = reason;
        } else if (requestType == "group") {
            action = "set_group_add_request";
            params["sub_type"] = subType;
            if (!reason.empty()) params["reason"] = reason;
        } else {
            return jade_text_create("{\"ok\":false,\"error\":\"不支持的请求类型\"}");
        }
        json r = !accountId.empty()
            ? call_onebot_api_bridge_for_account(accountId, action, params)
            : call_onebot_api_bridge(action, params);
        bool ok = plugin_retcode_ok(r) != 0;
        json out;
        out["ok"] = ok;
        out["response"] = r;
        if (!ok) out["error"] = r.value("wording", r.value("msg", "请求处理失败，请检查账号权限或 API 通道"));
        std::string who = accountId.empty() ? "" : account_display_name(accountId);
        add_log(ok ? "system" : "warning", who, std::string(approve ? "已同意" : "已拒绝") + (requestType == "friend" ? "好友请求" : "群请求"), 0, 0, accountId, who);
        return jade_text_create(out.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"请求处理异常\"}");
}

static const char* ipc_friend_query(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string accountId = j.value("account_id", j.value("accountId", ""));
        std::string action = j.value("action", "");
        json params = j.value("params", json::object());
        static const std::set<std::string> allowed = {
            "get_stranger_info",
            "get_friend_list"
        };
        if (allowed.find(action) == allowed.end()) {
            return jade_text_create("{\"ok\":false,\"error\":\"不支持的好友查询\"}");
        }
        if (!params.is_object()) params = json::object();
        json r = !accountId.empty()
            ? call_onebot_api_bridge_for_account(accountId, action, params)
            : call_onebot_api_bridge(action, params);
        bool ok = plugin_retcode_ok(r) != 0 || (r.is_object() && r.contains("data") && !r.contains("retcode"));
        json out;
        out["ok"] = ok;
        out["data"] = r.contains("data") ? r["data"] : r;
        out["response"] = r;
        if (!ok) out["error"] = r.value("wording", r.value("msg", "好友查询失败，请检查账号 API 通道"));
        return jade_text_create(out.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"好友查询异常\"}");
}

static const char* ipc_friend_action(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string accountId = j.value("account_id", j.value("accountId", ""));
        std::string action = j.value("action", "");
        json params = j.value("params", json::object());
        static const std::set<std::string> allowed = {
            "send_like",
            "delete_friend"
        };
        if (allowed.find(action) == allowed.end()) {
            return jade_text_create("{\"ok\":false,\"error\":\"不支持的好友操作\"}");
        }
        if (!params.is_object()) params = json::object();
        json r = !accountId.empty()
            ? call_onebot_api_bridge_for_account(accountId, action, params)
            : call_onebot_api_bridge(action, params);
        bool ok = plugin_retcode_ok(r) != 0;
        json out;
        out["ok"] = ok;
        out["response"] = r;
        if (!ok) out["error"] = r.value("wording", r.value("msg", "好友操作失败，请检查账号权限或 API 通道"));
        int64_t uid = json_value_i64(params, "user_id", 0);
        std::string who = accountId.empty() ? "" : account_display_name(accountId);
        add_log(ok ? "system" : "warning", who, std::string("好友操作 ") + action + (ok ? " 成功" : " 失败"), uid, 0, accountId, who);
        return jade_text_create(out.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"好友操作异常\"}");
}

static const char* ipc_export_logs(uint32_t wid, const char* data) {
    std::lock_guard<std::mutex> lock(g_mutex);
    // Build export text
    std::string out;
    out += "YuexBot Log Export - " + now_str() + "\n";
    out += "========================================\n\n";
    for (auto& e : g_logs) {
        out += "[" + e.time + "] [" + e.type + "]";
        if (!e.sender.empty()) out += " " + e.sender;
        out += ": " + e.content + "\n";
    }
    // Save to file
    std::string filename = "logs_export_" + std::to_string(time(nullptr)) + ".txt";
    FILE* f = fopen(filename.c_str(), "w");
    if (f) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        fwrite(bom, 1, sizeof(bom), f);
        fwrite(out.c_str(), 1, out.size(), f);
        fclose(f);
        json r; r["path"] = filename; r["count"] = (int)g_logs.size();
        add_log("system", "", "日志已导出: " + filename);
        return jade_text_create(r.dump().c_str());
    }
    json r; r["error"] = "open file failed";
    return jade_text_create(r.dump().c_str());
}

static const char* ipc_get_detailed_status(uint32_t wid, const char* data) {
    json r;
    r["connected"] = g_connected.load();
    r["uptime"] = uptime_str();
    r["messages"] = g_totalMessages.load();
    r["friends"] = (int)g_friends.size();
    r["groups"] = (int)g_groups.size();
    r["events"] = g_totalEvents.load();
    r["latency"] = g_latencyMs.load();
    r["host"] = g_host;
    r["port"] = g_port;
    r["path"] = g_wsPath;
    r["mode"] = g_connMode;
    r["event_port"] = g_eventServerPort;
    r["auto_reconnect"] = g_autoReconnect.load();
    r["event_server"] = g_eventServerRunning.load();
    if (g_connected) {
        auto info = ((g_connMode == "forward-ws" || g_connMode == "reverse-ws") && g_ws.isConnected())
                    ? callApiViaWs("get_login_info", json::object(), 3000)
                    : g_onebot.getLoginInfo();
        if (info.contains("data")) {
            auto d = info["data"];
            if (d.contains("user_id")) r["qq"] = json_value_string(d, "user_id");
            if (d.contains("nickname")) r["name"] = json_value_string(d, "nickname");
        }
    }
    return jade_text_create(r.dump().c_str());
}

static const char* ipc_run_connection_diagnostics(uint32_t wid, const char* data) {
    json r;
    r["ok"] = true;
    r["time"] = now_str();
    r["framework"] = {
        {"connected", g_connected.load()},
        {"mode", g_connMode},
        {"host", g_host},
        {"port", g_port},
        {"path", g_wsPath},
        {"event_server", g_eventServerRunning.load()},
        {"event_port", g_eventServerPort},
        {"reverse_ws_listening", g_reverseWsListening.load()},
        {"reverse_ws_port", g_reverseWsPort},
        {"auto_reconnect", g_autoReconnect.load()}
    };
    json settings = load_config_file("settings.json");
    r["global_auto_connect_accounts"] = settings.value("autoConnectAccounts", false);
    r["accounts"] = json::array();
    for (const auto& a : g_accounts) {
        json item;
        item["id"] = a.id;
        item["name"] = a.name;
        item["qq"] = a.qq;
        item["mode"] = a.mode;
        item["host"] = a.host;
        item["port"] = a.port;
        item["path"] = a.path.empty() ? "/onebot/v11/ws" : a.path;
        item["status"] = a.status;
        item["auto_connect"] = a.autoConnect;
        item["issues"] = json::array();
        item["suggestions"] = json::array();
        if (a.host.empty()) item["issues"].push_back("连接地址为空");
        if (a.port <= 0 || a.port > 65535) item["issues"].push_back("端口不在 1-65535 范围内");
        if (a.mode == "forward-ws" && item["path"].get<std::string>().empty()) item["issues"].push_back("正向 WS 路径为空");
        if (a.mode == "reverse-ws") item["suggestions"].push_back("反向 WS 目前走框架统一监听端口，多账号归属依赖 self_id/登录信息");
        auto rt = get_account_runtime(a.id);
        if (rt) {
            bool wsAlive = rt->ws && rt->ws->isConnected();
            bool httpAlive = rt->http && rt->http->isConnected();
            item["runtime_connected"] = rt->connected.load();
            item["runtime_connecting"] = rt->connecting.load();
            item["ws_connected"] = wsAlive;
            item["http_connected"] = httpAlive;
            item["ws_api_available"] = rt->wsApiAvailable.load();
            item["ws_messages"] = rt->wsMessages.load();
            item["ws_events"] = rt->wsEvents.load();
            item["ws_unknown"] = rt->wsUnknownMessages.load();
            item["friend_count"] = (int)rt->friends.size();
            item["group_count"] = (int)rt->groups.size();
            item["login_qq"] = rt->loginQq;
            item["nickname"] = rt->nickname;
            item["reconnect_attempts"] = rt->reconnectAttempts.load();
            item["manual_stopped"] = rt->manualStopped.load();
            item["last_ws_preview"] = rt->lastWsPreview;
            if (a.mode == "forward-ws" && wsAlive && !rt->wsApiAvailable.load()) {
                item["issues"].push_back("WS 事件通道已连接，但 WS API 不可用");
                item["suggestions"].push_back("如果 LLBot/NapCat 的 WS 只推事件，请在账号配置里填写 HTTP API 端口作为兜底");
            }
            if (a.status == "online" && !wsAlive && !httpAlive && a.mode != "reverse-ws") {
                item["issues"].push_back("账号显示在线，但底层传输未连接");
            }
            if (rt->wsMessages.load() > 0 && rt->wsEvents.load() == 0) {
                item["issues"].push_back("WS 有数据但没有解析出 OneBot 事件");
                item["suggestions"].push_back("检查 WS 路径是否接入事件通道，NapCat/LLBot 配置需开启事件上报");
            }
            if (rt->manualStopped.load()) item["suggestions"].push_back("该账号已手动停止，自动重连不会拉起它");
        } else {
            item["issues"].push_back("运行时对象未创建");
        }
        item["healthy"] = item["issues"].empty();
        r["accounts"].push_back(item);
    }
    add_log("system", "", "已生成连接诊断报告");
    return jade_text_create(r.dump().c_str());
}

static const char* ipc_get_plugins(uint32_t wid, const char* data) {
    json r = plugins_to_json();
    return jade_text_create(r.dump().c_str());
}

static const char* ipc_set_plugin_enabled(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string id = j.value("id", "");
        bool enabled = j.value("enabled", false);
        bool ok = set_plugin_enabled_runtime(id, enabled);
        json payload = plugins_to_json();
        json r = payload;
        r["ok"] = ok;
        if (!ok) {
            std::string opError;
            { std::lock_guard<std::mutex> lock(g_pluginOpMutex); opError = g_lastPluginOpError; }
            ReservedPluginRuntime reserved;
            if (get_reserved_plugin_copy(id, reserved)) {
                r["error"] = opError.empty() ? reserved_plugin_error_message(reserved, enabled ? "启用" : "切换状态") : opError;
            } else {
                r["error"] = opError.empty() ? (enabled ? "插件启用失败：插件初始化返回失败，请查看日志或检查插件权限/依赖" : "插件状态切换失败") : opError;
            }
        }
        push_to_frontend("plugins-updated", payload.dump());
        return jade_text_create(r.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false}");
}

static const char* ipc_reload_plugin(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string id = j.value("id", "");
        bool found = false;
        std::string path;
        bool enabled = false;
        {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            auto* p = find_plugin_locked(id);
            if (p) {
                found = true;
                path = p->path;
                enabled = p->enabled;
            }
        }
        scan_plugins();
        if (!path.empty()) {
            std::lock_guard<std::mutex> lock(g_pluginMutex);
            auto* np = find_plugin_locked(id);
            if (np && enabled && !np->enabled && np->init) np->enabled = np->init(&g_pluginApi) != 0;
        }
        json r; r["ok"] = found || id.empty();
        push_to_frontend("plugins-updated", plugins_to_json().dump());
        return jade_text_create(r.dump().c_str());
    } catch (...) {}
    scan_plugins();
    return jade_text_create("{\"ok\":true}");
}

static const char* ipc_open_plugin_dir(uint32_t wid, const char* data) {
    (void)wid; (void)data;
    ensure_directory_tree(g_pluginDir);
    ShellExecuteA(NULL, "open", g_pluginDir.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return jade_text_create("{\"ok\":true}");
}

static const char* ipc_install_plugin(uint32_t wid, const char* data) {
    try {
        json r = install_plugin_from_dialog();
        push_to_frontend("plugins-updated", plugins_to_json().dump());
        return jade_text_create(r.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"添加插件失败\"}");
}

static const char* ipc_open_plugin_settings(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data ? data : "{}");
        std::string id = j.value("id", "");
        json r = open_plugin_settings_runtime(id);
        return jade_text_create(r.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"打开插件设置失败\"}");
}

static const char* ipc_set_plugin_event_filter(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data ? data : "{}");
        std::string id = j.value("id", "");
        if (id.empty()) return jade_text_create("{\"ok\":false,\"error\":\"插件 ID 为空\"}");
        int ok = plugin_set_event_filter(id.c_str(), default_plugin_event_mask());
        json r;
        r["ok"] = ok != 0;
        r["message"] = "事件订阅已固定为全量，插件默认接收全部消息和事件";
        if (!ok) r["error"] = "更新事件订阅模式失败";
        push_to_frontend("plugins-updated", plugins_to_json().dump());
        return jade_text_create(r.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"更新事件订阅模式异常\"}");
}

static const char* ipc_delete_plugin(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data ? data : "{}");
        std::string id = j.value("id", "");
        json r = delete_plugin_runtime(id);
        push_to_frontend("plugins-updated", plugins_to_json().dump());
        return jade_text_create(r.dump().c_str());
    } catch (...) {}
    return jade_text_create("{\"ok\":false,\"error\":\"删除插件失败\"}");
}

// ============================================================
// Account IPC Handlers
// ============================================================
static const char* ipc_get_accounts(uint32_t wid, const char* data) {
    return jade_text_create(accounts_to_json().dump().c_str());
}

static const char* ipc_save_account(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        AccountConfig a;
        a.id = j.value("id", "");
        a.name = j.value("name", "");
        a.qq = j.value("qq", "");
        a.mode = j.value("mode", "reverse-ws");
        a.host = j.value("host", "127.0.0.1");
        a.port = json_get_int(j, "port", 3001);
        a.path = json_get_ws_path(j);
        a.token = j.value("token", "");
        a.apiHost = j.value("api_host", j.value("apiHost", ""));
        a.apiPort = json_get_int(j, "api_port", json_get_int(j, "apiPort", 0));
        a.apiToken = j.value("api_token", j.value("apiToken", ""));
        a.autoConnect = j.value("auto_connect", j.value("autoConnect", false));
        a.status = "offline";
        if (a.name.empty() && a.qq.empty()) {
            return jade_text_create("{\"success\":false,\"error\":\"account name or QQ is empty\"}");
        }
        if (a.host.empty()) {
            return jade_text_create("{\"success\":false,\"error\":\"API address cannot be empty\"}");
        }
        if (a.port <= 0 || a.port > 65535) {
            return jade_text_create("{\"success\":false,\"error\":\"invalid port\"}");
        }
        if (a.apiPort < 0 || a.apiPort > 65535) {
            return jade_text_create("{\"success\":false,\"error\":\"invalid api port\"}");
        }
        int editIndex = j.value("index", -1);
        if (a.id.empty() && editIndex >= 0 && editIndex < (int)g_accounts.size()) {
            a.id = g_accounts[editIndex].id;
        }
        if (a.id.empty()) {
            a.id = !a.qq.empty() ? ("acc_" + a.qq)
                                 : ("acc_" + std::to_string(time(nullptr)) + "_" + std::to_string(g_accounts.size()));
        }

        int idx = find_account(a.id);
        if (editIndex >= 0 && editIndex < (int)g_accounts.size()) idx = editIndex;
        if (idx >= 0) {
            a.status = g_accounts[idx].status; // preserve status
            g_accounts[idx] = a;
            sync_account_runtimes_from_configs();
            add_log("system", "", "账号配置已更新: " + (a.name.empty() ? a.qq : a.name));
        } else {
            g_accounts.push_back(a);
            sync_account_runtimes_from_configs();
            add_log("system", "", "账号配置已添加: " + (a.name.empty() ? a.qq : a.name));
        }
        save_accounts_to_yaml();
        push_to_frontend("accounts-updated", accounts_to_json().dump());
        return jade_text_create("{\"success\":true}");
    } catch (...) {}
    return jade_text_create("{\"success\":false}");
}

static const char* ipc_delete_account(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        std::string id = j.value("id", "");
        int idx = j.value("index", -1);
        if (idx < 0) idx = find_account(id);
        if (idx >= 0 && idx < (int)g_accounts.size()) {
            std::string accId = g_accounts[idx].id;
            auto rt = get_account_runtime(accId);
            if (rt) {
                rt->manualStopped = true;
                rt->nextReconnectAt = 0;
                if (rt->ws && rt->ws->isConnected()) rt->ws->disconnect();
                if (rt->http && rt->http->isConnected()) rt->http->disconnect();
            }
            if (accId == g_activeAccountId && (g_connMode == "reverse-ws" || !rt)) {
                do_disconnect();
                g_activeAccountId.clear();
            }
            g_accounts.erase(g_accounts.begin() + idx);
            sync_account_runtimes_from_configs();
            save_accounts_to_yaml();
            add_log("system", "", "账号配置已删除");
        } else {
            return jade_text_create("{\"success\":false,\"error\":\"account not found\"}");
        }
    } catch (...) {}
    push_to_frontend("accounts-updated", accounts_to_json().dump());
    return jade_text_create("{\"success\":true}");
}

static const char* ipc_connect_account(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        // Support both index and id lookup
        int idx = j.value("index", -1);
        if (idx < 0) {
            std::string id = j.value("id", "");
            idx = find_account(id);
        }
        if (idx >= 0 && idx < (int)g_accounts.size()) {
            auto& a = g_accounts[idx];
            a.status = "connecting";
            g_activeAccountId = a.id;

            // Use account's own connection mode
            std::string mode = a.mode;
            std::string host = a.host;
            int port = a.port;
            std::string token = a.token;
            std::string path = a.path.empty() ? "/onebot/v11/ws" : a.path;

            sync_account_runtimes_from_configs();
            save_accounts_to_yaml();
            push_to_frontend("accounts-updated", accounts_to_json().dump());

            if (mode == "forward-ws" || mode == "http-post") {
                connect_account_runtime(a.id);
            } else {
                g_host = host; g_port = port; g_token = token; g_wsPath = normalize_ws_path(path);
                g_connMode = mode;
                g_autoReconnect = true;
                add_log("system", account_display_name(a.id), "连接账号 [" + mode + "] " + host + ":" + std::to_string(port), 0, 0, a.id, account_display_name(a.id));
                do_connect(host, port, token, mode, path);
            }
        }
    } catch (...) {}
    return jade_text_create("{\"success\":true}");
}

static const char* ipc_disconnect_account(uint32_t wid, const char* data) {
    try {
        auto j = json::parse(data);
        int idx = j.value("index", -1);
        if (idx < 0) {
            std::string id = j.value("id", "");
            idx = find_account(id);
        }
        bool disconnectCurrent = (idx < 0) || (idx < (int)g_accounts.size() && g_accounts[idx].id == g_activeAccountId);
        if (idx >= 0 && idx < (int)g_accounts.size()) {
            std::string accId = g_accounts[idx].id;
            auto rt = get_account_runtime(accId);
            if (rt) {
                rt->manualStopped = true;
                rt->nextReconnectAt = 0;
                if (rt->ws && rt->ws->isConnected()) rt->ws->disconnect();
                if (rt->http && rt->http->isConnected()) rt->http->disconnect();
                rt->connected = false;
                rt->connecting = false;
            }
            g_accounts[idx].status = "offline";
            update_account_runtime_status(accId, "offline");
            save_accounts_to_yaml();
        }
        if (disconnectCurrent) {
            if (g_connMode == "reverse-ws") stop_reverse_ws_listener();
            if (g_ws.isConnected()) g_ws.disconnect();
            g_onebot.disconnect();
            g_autoReconnect = false;
            g_connected = false;
            g_activeAccountId.clear();
            add_log("system", "", "已断开连接");
            json status;
            status["connected"] = false;
            push_to_frontend("status-changed", status.dump());
        }
        push_to_frontend("accounts-updated", accounts_to_json().dump());
    } catch (...) {}
    return jade_text_create("{\"success\":true}");
}
// ============================================================
// Entry Point
// ============================================================
const char* app_ready_callback(uint32_t window_id, const char* event_data) {
    startup_trace("app-ready window_id=%u data=%s", window_id, event_data ? event_data : "(null)");
    if (!event_data || strcmp(event_data, "success") != 0) {
        printf("JadeView init failed: %s\n", event_data ? event_data : "unknown");
        startup_trace("app-ready rejected");
        return NULL;
    }

    // Register all IPC handlers
    register_ipc_handler("connect", ipc_connect);
    register_ipc_handler("disconnect", ipc_disconnect);
    register_ipc_handler("quit", ipc_quit);
    register_ipc_handler("get-status", ipc_get_status);
    register_ipc_handler("get-friends", ipc_get_friends);
    register_ipc_handler("get-groups", ipc_get_groups);
    register_ipc_handler("get-logs", ipc_get_logs);
    register_ipc_handler("clear-logs", ipc_clear_logs);
    register_ipc_handler("compact-memory", ipc_compact_memory);
    register_ipc_handler("send-message", ipc_send_message);
    register_ipc_handler("save-settings", ipc_save_settings);
    register_ipc_handler("load-settings", ipc_load_settings);
    register_ipc_handler("check-service", ipc_check_service);
    register_ipc_handler("get-group-members", ipc_get_group_members);
    register_ipc_handler("get-group-info", ipc_get_group_info);
    register_ipc_handler("group-action", ipc_group_action);
    register_ipc_handler("group-query", ipc_group_query);
    register_ipc_handler("request-action", ipc_request_action);
    register_ipc_handler("friend-query", ipc_friend_query);
    register_ipc_handler("friend-action", ipc_friend_action);
    register_ipc_handler("export-logs", ipc_export_logs);
    register_ipc_handler("get-detailed-status", ipc_get_detailed_status);
    register_ipc_handler("run-connection-diagnostics", ipc_run_connection_diagnostics);
    register_ipc_handler("get-plugins", ipc_get_plugins);
    register_ipc_handler("set-plugin-enabled", ipc_set_plugin_enabled);
    register_ipc_handler("reload-plugin", ipc_reload_plugin);
    register_ipc_handler("open-plugin-dir", ipc_open_plugin_dir);
    register_ipc_handler("install-plugin", ipc_install_plugin);
    register_ipc_handler("open-plugin-settings", ipc_open_plugin_settings);
    register_ipc_handler("set-plugin-event-filter", ipc_set_plugin_event_filter);
    register_ipc_handler("delete-plugin", ipc_delete_plugin);
    register_ipc_handler("get-accounts", ipc_get_accounts);
    register_ipc_handler("save-account", ipc_save_account);
    register_ipc_handler("delete-account", ipc_delete_account);
    register_ipc_handler("connect-account", ipc_connect_account);
    register_ipc_handler("disconnect-account", ipc_disconnect_account);

    // Resolve release layout and keep working directory at the main root.
    initialize_runtime_paths();
    init_plugin_api();
    startup_trace("ipc and paths ready package=%s root=%s bin=%s data=%s corn=%s plugin=%s ui=%s",
                  g_packageDir.c_str(), g_rootDir.c_str(), g_binDir.c_str(), g_configDir.c_str(),
                  g_cornDir.c_str(), g_pluginDir.c_str(), g_uiDir.c_str());

    // Load embedded frontend. The release package no longer needs to carry
    // www/index.html; it is kept only as a development fallback.
    char url_buf[1024] = {0};
    std::string frontend_url;
    std::string embedded_dir;
    std::string embedded_index;
    bool embedded_ok = write_embedded_frontend(embedded_dir, embedded_index);
    if (embedded_ok) {
        if (set_protocol_service_path) {
            int32_t result = set_protocol_service_path(embedded_dir.c_str(), url_buf, sizeof(url_buf));
            if (result == 1) {
                frontend_url = url_buf;
                startup_trace("embedded frontend protocol dir=%s", embedded_dir.c_str());
            }
        }
        if (frontend_url.empty()) {
            std::string file_path = embedded_index;
            std::replace(file_path.begin(), file_path.end(), '\\', '/');
            frontend_url = "file:///" + file_path;
            snprintf(url_buf, sizeof(url_buf), "%s", frontend_url.c_str());
            startup_trace("embedded frontend file=%s", embedded_index.c_str());
        }
    } else {
        startup_trace("embedded frontend write failed, falling back to external www");
    }
    if (frontend_url.empty()) {
        if (set_protocol_service_path) {
            std::string www_path = g_uiDir;
            int32_t result = set_protocol_service_path(www_path.c_str(), url_buf, sizeof(url_buf));
            if (result != 1) {
                printf("Failed to load embedded UI and www directory\n");
                startup_trace("set_protocol_service_path fallback failed");
                return NULL;
            }
            frontend_url = url_buf;
        } else {
        #if defined(_WIN64)
            std::string file_path = path_join(g_uiDir, "index.html");
        #else
            std::string file_path = path_join(g_uiDir, "index.html");
        #endif
            std::replace(file_path.begin(), file_path.end(), '\\', '/');
            frontend_url = "file:///" + file_path;
            snprintf(url_buf, sizeof(url_buf), "%s", frontend_url.c_str());
        }
    }
    printf("Frontend URL: %s\n", url_buf);
    startup_trace("frontend url=%s", url_buf);

    // Create window
    WebViewWindowOptions opts = {};
    opts.title = "YuexBot";
    opts.width = 1366;
    opts.height = 900;
    opts.resizable = 1;
    opts.frame_style = "title-overlay";
    opts.theme = "Light";
    opts.x = -1;
    opts.y = -1;
    opts.min_width = 960;
    opts.min_height = 600;
    opts.focus = 1;
    opts.maximized = 0;

    WebViewSettings ws = {};
    ws.autoplay = 0;
    ws.background_throttling = 0;
    ws.disable_right_click = 0;

    #if defined(_WIN64)
    g_windowId = create_webview_window(url_buf, 0, &opts, &ws);
    #else
    g_windowId = create_webview_window(url_buf, 0, nullptr, nullptr);
    #endif
    if (g_windowId == 0) {
        printf("Failed to create window\n");
        startup_trace("create_webview_window failed");
        return NULL;
    }
    printf("Window created, id=%u\n", g_windowId);
    startup_trace("window created id=%u", g_windowId);

    scan_plugins();
    startup_trace("plugins scanned");
    start_plugin_event_worker();

    // Load saved settings (custom file first, then yaml_get)
    startup_trace("loading saved config");
    {
        auto cfg = load_config_file("config.json");
        if (cfg.contains("host")) g_host = strip_quotes(cfg["host"].get<std::string>());
        if (cfg.contains("port")) g_port = cfg["port"].get<int>();
        if (cfg.contains("token")) g_token = strip_quotes(cfg["token"].get<std::string>());
        g_wsPath = json_get_ws_path(cfg, g_wsPath);
    }
    if (yaml_get) {
        char buf[512];
        if (yaml_get("config.yaml", "connection.host", buf, sizeof(buf)) > 0) g_host = buf;
        if (yaml_get("config.yaml", "connection.port", buf, sizeof(buf)) > 0) { try { g_port = std::stoi(buf); } catch (...) {} }
        if (yaml_get("config.yaml", "connection.token", buf, sizeof(buf)) > 0) g_token = buf;
    }

    // Start background threads
    startup_trace("starting background threads");
    #if defined(_WIN64)
    std::thread(poll_thread).detach();
    std::thread(stats_thread).detach();
    std::thread(event_server_thread).detach();
    std::thread(account_reconnect_watch_thread).detach();
    #else
    startup_trace("background threads skipped on x86 test build");
    #endif

    // Load accounts
    startup_trace("loading accounts");
    load_accounts_from_yaml();

    #if defined(_WIN64)
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        if (g_shuttingDown) return;
        // Auto-connect using saved settings after the WebView window is stable.
        startup_trace("auto-connect begin");
        try {
        auto settings = load_config_file("settings.json");
        bool autoConnectAccounts = settings.value("autoConnectAccounts", false);
        bool hasAccountAutoConnect = false;
        for (const auto& a : g_accounts) {
            if (a.autoConnect) {
                hasAccountAutoConnect = true;
                break;
            }
        }
        if ((autoConnectAccounts || hasAccountAutoConnect) && !g_accounts.empty()) {
            int started = 0;
            for (auto& a : g_accounts) {
                if (!autoConnectAccounts && !a.autoConnect) continue;
                started++;
                std::string mode = a.mode.empty() ? "reverse-ws" : a.mode;
                g_activeAccountId = a.id;
                add_log("system", account_display_name(a.id), "自动连接账号: " + (a.name.empty() ? a.qq : a.name), 0, 0, a.id, account_display_name(a.id));
                if (mode == "forward-ws" || mode == "http-post") {
                    connect_account_runtime(a.id);
                } else {
                    g_host = a.host;
                    g_port = a.port;
                    g_token = a.token;
                    g_connMode = "reverse-ws";
                    g_wsPath = normalize_ws_path(a.path.empty() ? g_wsPath : a.path);
                    g_autoReconnect = true;
                    do_connect(a.host, a.port, a.token, g_connMode, g_wsPath);
                }
            }
            if (started == 0) {
                add_log("system", "", "未找到需要自动连接的账号");
                g_autoReconnect = false;
                return;
            }
            return;
        }
        if (settings.contains("connections")) {
            auto& c = settings["connections"];
            // Try each connection mode in order
            if (c.contains("reverseWs") && c["reverseWs"].value("enabled", false)) {
                auto& rw = c["reverseWs"];
                std::string h = rw.value("host", "0.0.0.0");
                int p = json_get_int(rw, "port", 3001);
                std::string t = rw.value("token", "");
                g_connMode = "reverse-ws";
                g_autoReconnect = true;
                add_log("system", "", "自动连接配置: 反向 WS " + h + ":" + std::to_string(p));
                do_connect(h, p, t, "reverse-ws");
            } else if (c.contains("forwardWs") && c["forwardWs"].value("enabled", false)) {
                auto& fw = c["forwardWs"];
                std::string h = fw.value("host", "127.0.0.1");
                int p = json_get_int(fw, "port", 3001);
                std::string t = fw.value("token", "");
                std::string path = json_get_ws_path(fw, g_wsPath);
                g_connMode = "forward-ws";
                g_autoReconnect = true;
                add_log("system", "", "自动连接配置: 正向 WS " + h + ":" + std::to_string(p) + path);
                do_connect(h, p, t, "forward-ws", path);
            } else if (c.contains("httpPost") && c["httpPost"].value("enabled", false)) {
                auto& hp = c["httpPost"];
                std::string h = hp.value("host", "127.0.0.1");
                int p = json_get_int(hp, "port", 3001);
                std::string t = hp.value("token", "");
                g_connMode = "http-post";
                g_autoReconnect = true;
                add_log("system", "", "自动连接配置: HTTP POST " + h + ":" + std::to_string(p));
                do_connect(h, p, t, "http-post");
            }
        } else if (autoConnectAccounts && !g_accounts.empty()) {
            auto& a = g_accounts[0];
            g_host = a.host; g_port = a.port; g_token = a.token;
            g_autoReconnect = true;
            add_log("system", "", "自动连接账号: " + (a.name.empty() ? a.qq : a.name));
            do_connect(a.host, a.port, a.token, a.mode, a.path);
        } else {
            g_autoReconnect = false;
            add_log("system", "", "未开启自动连接，等待手动连接账号或连接配置");
        }
        } catch (const std::exception& e) {
            startup_trace("auto-connect exception=%s", e.what());
        } catch (...) {
            startup_trace("auto-connect unknown exception");
        }
    }).detach();
    #else
    startup_trace("auto-connect skipped on x86 test build");
    #endif
    startup_trace("app-ready complete");

    return NULL;
}

const char* window_close_callback(uint32_t window_id, const char* event_data) {
    (void)window_id;
    (void)event_data;
    startup_trace("window-close callback");
    request_process_exit("window close");
    return NULL;
}

const char* window_all_closed_callback(uint32_t window_id, const char* event_data) {
    (void)window_id;
    (void)event_data;
    startup_trace("window-all-closed callback");
    request_process_exit("all windows closed");
    return NULL;
}

int main() {
    SetUnhandledExceptionFilter(unhandled_exception_filter);
    std::set_terminate(yuex_terminate_handler);
    g_startTime = time(nullptr);
    startup_trace("main entered");
    startup_trace("initialize_runtime_paths begin");
    initialize_runtime_paths();
    startup_trace("initialize_runtime_paths end");
    startup_trace("main start");

    // Disable WebView2 sandbox - must be set before ANY COM/WebView2 init
    std::string webviewDataDir = path_join(g_configDir, "JadeView_data");
    ensure_directory_tree(webviewDataDir);
    SetEnvironmentVariableA("WEBVIEW2_USER_DATA_FOLDER", webviewDataDir.c_str());
    SetEnvironmentVariableA("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS",
        "--no-sandbox --disable-gpu-sandbox --disable-features=RendererCodeIntegrity "
        "--disable-extensions --disable-background-networking --no-first-run");

    if (!load_jade_dll()) {
        printf("Failed to load JadeView DLL\n");
        startup_trace("load %s failed error=%lu missing=%s",
                      g_jadeDllName ? g_jadeDllName : "JadeView",
                      (unsigned long)g_jadeLastError,
                      g_jadeMissingSymbol ? g_jadeMissingSymbol : "");
        return 1;
    }
    startup_trace("%s loaded", g_jadeDllName ? g_jadeDllName : "JadeView");

    startup_trace("register jade callbacks begin");
    jade_on("app-ready", app_ready_callback);
    jade_on("window-close", window_close_callback);
    jade_on("window-all-closed", window_all_closed_callback);
    startup_trace("app-ready handler registered");

    // Kill old YuexBot zombie processes that may interfere with WebView2
    {
        DWORD myPid = GetCurrentProcessId();
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32 pe = {};
            pe.dwSize = sizeof(pe);
            if (Process32First(hSnap, &pe)) {
                do {
                    if (pe.th32ProcessID != myPid && _stricmp(pe.szExeFile, "YuexBot.exe") == 0) {
                        startup_trace("killing old YuexBot process PID=%u", pe.th32ProcessID);
                        HANDLE hProc = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                        if (hProc) { TerminateProcess(hProc, 1); CloseHandle(hProc); }
                    }
                } while (Process32Next(hSnap, &pe));
            }
            CloseHandle(hSnap);
        }
        Sleep(500); // Wait for processes to terminate
    }

    // Try JadeView init with retry
    int result = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        #if defined(_WIN64)
        result = JadeView_init(0, NULL, NULL, "YuexBot", "com.yuexbot.framework", 0);
        #else
        result = JadeView_init(0, NULL, NULL);
        #endif
        startup_trace("JadeView_init attempt=%d result=%d", attempt + 1, result);
        if (result != 0) break;
        Sleep(1000);
    }
    if (result == 0) {
        printf("JadeView init failed\n");
        startup_trace("JadeView_init returned 0");
        return 1;
    }
    startup_trace("JadeView_init returned %d", result);

    run_message_loop();
    startup_trace("message loop exited");

    shutdown_runtime();
    unload_jade_dll();
    ExitProcess(0);
    return 0;
}



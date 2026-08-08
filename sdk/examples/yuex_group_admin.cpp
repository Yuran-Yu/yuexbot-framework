#include "../yuex_plugin_sdk.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

static const YuexBotApi* g_api = nullptr;
static const char* kPluginId = "yuex_group_admin";
static HINSTANCE g_instance = nullptr;
static HWND g_settingsWindow = nullptr;
static std::mutex g_configMutex;
static std::mutex g_ownerMutex;
static std::vector<std::string> g_ownerCache;
struct CommandJob {
    std::string accountId;
    long long groupId = 0;
    std::string rawMessage;
};
static std::mutex g_jobMutex;
static std::condition_variable g_jobCv;
static std::deque<CommandJob> g_jobs;
static std::thread g_worker;
static std::atomic<bool> g_workerRunning{false};
static std::string g_dataDir;

struct BanEntry {
    long long userId = 0;
    long long endTime = 0;
};

static std::wstring utf8_to_wide(const std::string& text) {
    if (text.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (len <= 0) return L"";
    std::wstring out((size_t)len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), len);
    return out;
}

static std::string wide_to_utf8(const std::wstring& text) {
    if (text.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string out((size_t)len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), len, nullptr, nullptr);
    return out;
}

static std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    if (last == '\\' || last == '/') return a + b;
    return a + "\\" + b;
}

static std::string config_path() {
    return path_join(g_dataDir, "config.json");
}

static std::string read_text_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static bool write_text_file(const std::string& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(text.data(), (std::streamsize)text.size());
    return !!out;
}

static std::string trim_copy(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    return s;
}

static std::string digits_only(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c >= '0' && c <= '9') out.push_back(c);
    }
    return out;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(c); break;
        }
    }
    return out;
}

static std::string json_unescape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '\\' || i + 1 >= s.size()) {
            out.push_back(s[i]);
            continue;
        }
        char n = s[++i];
        if (n == 'n') out.push_back('\n');
        else if (n == 'r') out.push_back('\r');
        else if (n == 't') out.push_back('\t');
        else out.push_back(n);
    }
    return out;
}

static std::vector<std::string> parse_owner_list_text(const std::string& raw) {
    std::vector<std::string> out;
    std::string token;
    auto flush = [&]() {
        std::string qq = digits_only(trim_copy(token));
        token.clear();
        if (qq.empty()) return;
        if (std::find(out.begin(), out.end(), qq) == out.end()) out.push_back(qq);
    };
    for (char c : raw) {
        if (c == ',' || c == ';' || c == '\n' || c == '\r' || c == ' ' || c == '\t') flush();
        else token.push_back(c);
    }
    flush();
    return out;
}

static std::vector<std::string> load_owners() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    std::vector<std::string> owners;
    std::string raw = read_text_file(config_path());
    size_t key = raw.find("\"owners\"");
    if (key == std::string::npos) return owners;
    size_t lb = raw.find('[', key);
    size_t rb = raw.find(']', lb == std::string::npos ? key : lb);
    if (lb == std::string::npos || rb == std::string::npos || rb <= lb) return owners;
    std::string body = raw.substr(lb + 1, rb - lb - 1);
    size_t pos = 0;
    while (pos < body.size()) {
        size_t q1 = body.find('"', pos);
        if (q1 == std::string::npos) break;
        size_t q2 = q1 + 1;
        bool esc = false;
        for (; q2 < body.size(); ++q2) {
            if (esc) {
                esc = false;
                continue;
            }
            if (body[q2] == '\\') {
                esc = true;
                continue;
            }
            if (body[q2] == '"') break;
        }
        if (q2 >= body.size()) break;
        std::string qq = digits_only(json_unescape(body.substr(q1 + 1, q2 - q1 - 1)));
        if (!qq.empty() && std::find(owners.begin(), owners.end(), qq) == owners.end()) owners.push_back(qq);
        pos = q2 + 1;
    }
    return owners;
}

static void refresh_owner_cache() {
    auto owners = load_owners();
    std::lock_guard<std::mutex> lock(g_ownerMutex);
    g_ownerCache = std::move(owners);
}

static bool save_owners(const std::vector<std::string>& owners) {
    std::lock_guard<std::mutex> lock(g_configMutex);
    std::string raw = "{\r\n  \"owners\": [";
    for (size_t i = 0; i < owners.size(); ++i) {
        if (i) raw += ", ";
        raw += "\"" + json_escape(owners[i]) + "\"";
    }
    raw += "]\r\n}\r\n";
    bool ok = write_text_file(config_path(), raw);
    if (ok) {
        std::lock_guard<std::mutex> lock(g_ownerMutex);
        g_ownerCache = owners;
    }
    return ok;
}

static std::string owners_to_edit_text() {
    std::lock_guard<std::mutex> lock(g_ownerMutex);
    auto owners = g_ownerCache;
    std::string out;
    for (size_t i = 0; i < owners.size(); ++i) {
        if (i) out += "\r\n";
        out += owners[i];
    }
    return out;
}

static std::string json_string_field(const char* jsonText, const char* key) {
    if (!jsonText || !key) return "";
    std::string raw(jsonText);
    std::string needle = std::string("\"") + key + "\":";
    size_t pos = raw.find(needle);
    if (pos == std::string::npos) return "";
    pos = raw.find('"', pos + needle.size());
    if (pos == std::string::npos) return "";
    size_t start = pos + 1;
    bool esc = false;
    for (++pos; pos < raw.size(); ++pos) {
        if (esc) {
            esc = false;
            continue;
        }
        if (raw[pos] == '\\') {
            esc = true;
            continue;
        }
        if (raw[pos] == '"') return json_unescape(raw.substr(start, pos - start));
    }
    return "";
}

static long long json_i64_field(const char* jsonText, const char* key) {
    if (!jsonText || !key) return 0;
    std::string raw(jsonText);
    std::string needle = std::string("\"") + key + "\":";
    size_t pos = raw.find(needle);
    if (pos == std::string::npos) return 0;
    pos += needle.size();
    while (pos < raw.size() && (raw[pos] == ' ' || raw[pos] == '"')) pos++;
    return std::strtoll(raw.c_str() + pos, nullptr, 10);
}

static bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static std::vector<long long> extract_numbers(const std::string& s) {
    std::vector<long long> out;
    std::string token;
    for (char c : s) {
        if (c >= '0' && c <= '9') token.push_back(c);
        else if (!token.empty()) {
            out.push_back(std::strtoll(token.c_str(), nullptr, 10));
            token.clear();
        }
    }
    if (!token.empty()) out.push_back(std::strtoll(token.c_str(), nullptr, 10));
    return out;
}

static long long extract_cq_at(const std::string& s, size_t* endPos = nullptr) {
    size_t pos = s.find("[CQ:at");
    if (pos == std::string::npos) return 0;
    size_t end = s.find(']', pos);
    if (end == std::string::npos) return 0;
    size_t qq = s.find("qq=", pos);
    if (qq == std::string::npos || qq > end) return 0;
    qq += 3;
    size_t stop = qq;
    while (stop < end && s[stop] >= '0' && s[stop] <= '9') stop++;
    if (endPos) *endPos = end + 1;
    return std::strtoll(s.substr(qq, stop - qq).c_str(), nullptr, 10);
}

static void log_line(const char* level, const std::string& msg) {
    if (g_api && g_api->log) g_api->log(level, msg.c_str());
}

static void reply_group(const std::string& accountId, long long groupId, const std::string& msg) {
    if (!g_api || !g_api->send_group_msg_as || groupId <= 0) return;
    if (!g_api->send_group_msg_as(accountId.c_str(), groupId, msg.c_str()) && g_api->get_last_error) {
        log_line("error", std::string("群管回复失败: ") + g_api->get_last_error());
    }
}

static std::vector<BanEntry> parse_ban_entries(const std::string& raw) {
    std::vector<BanEntry> out;
    size_t pos = 0;
    long long now = (long long)std::time(nullptr);
    while (true) {
        size_t u = raw.find("\"user_id\":", pos);
        if (u == std::string::npos) break;
        size_t objStart = raw.rfind('{', u);
        size_t objEnd = raw.find('}', u);
        if (objStart == std::string::npos || objEnd == std::string::npos) {
            pos = u + 10;
            continue;
        }
        std::string obj = raw.substr(objStart, objEnd - objStart + 1);
        long long userId = json_i64_field(obj.c_str(), "user_id");
        long long shut = json_i64_field(obj.c_str(), "shut_up_timestamp");
        if (shut <= 0) shut = json_i64_field(obj.c_str(), "shut_up_time");
        if (userId > 0 && shut > now) out.push_back({userId, shut});
        pos = objEnd + 1;
    }
    return out;
}

static std::vector<BanEntry> get_ban_entries(const std::string& accountId, long long groupId) {
    if (!g_api || !g_api->get_group_member_list_as) return {};
    const char* res = g_api->get_group_member_list_as(accountId.c_str(), groupId);
    if (!res || !*res) return {};
    return parse_ban_entries(res);
}

static void handle_command(const std::string& accountId, long long groupId, const std::string& rawMessage) {
    if (rawMessage == "全体禁言") {
        int ok = g_api && g_api->set_group_whole_ban_as
            ? g_api->set_group_whole_ban_as(accountId.c_str(), groupId, 1)
            : 0;
        reply_group(accountId, groupId, ok ? "已开启全体禁言" : "全体禁言失败，请确认机器人是否有群管理权限");
        return;
    }
    if (rawMessage == "全体解禁") {
        int ok = g_api && g_api->set_group_whole_ban_as
            ? g_api->set_group_whole_ban_as(accountId.c_str(), groupId, 0)
            : 0;
        reply_group(accountId, groupId, ok ? "已关闭全体禁言" : "全体解禁失败，请确认机器人是否有群管理权限");
        return;
    }
    if (rawMessage == "禁言列表") {
        auto bans = get_ban_entries(accountId, groupId);
        if (bans.empty()) {
            reply_group(accountId, groupId, "当前没有从群成员列表中识别到正在禁言的成员");
            return;
        }
        std::ostringstream ss;
        ss << "当前禁言列表：";
        size_t limit = std::min<size_t>(bans.size(), 20);
        for (size_t i = 0; i < limit; ++i) {
            long long remain = std::max<long long>(0, bans[i].endTime - (long long)std::time(nullptr));
            ss << "\n" << (i + 1) << ". " << bans[i].userId << "，剩余约 " << (remain / 60) << " 分钟";
        }
        if (bans.size() > limit) ss << "\n其余 " << (bans.size() - limit) << " 人未显示";
        reply_group(accountId, groupId, ss.str());
        return;
    }
    if (rawMessage == "一键解禁") {
        auto bans = get_ban_entries(accountId, groupId);
        if (bans.empty()) {
            reply_group(accountId, groupId, "当前没有可一键解禁的成员");
            return;
        }
        int okCount = 0;
        for (const auto& b : bans) {
            if (g_api && g_api->set_group_ban_as &&
                g_api->set_group_ban_as(accountId.c_str(), groupId, b.userId, 0)) {
                okCount++;
            }
        }
        std::ostringstream ss;
        ss << "一键解禁完成，成功 " << okCount << "/" << bans.size() << " 人";
        reply_group(accountId, groupId, ss.str());
        return;
    }

    if (starts_with(rawMessage, "解禁")) {
        long long target = extract_cq_at(rawMessage);
        if (target <= 0) {
            auto nums = extract_numbers(rawMessage);
            if (!nums.empty()) target = nums[0];
        }
        if (target <= 0) {
            reply_group(accountId, groupId, "用法：解禁@某人");
            return;
        }
        int ok = g_api && g_api->set_group_ban_as
            ? g_api->set_group_ban_as(accountId.c_str(), groupId, target, 0)
            : 0;
        reply_group(accountId, groupId, ok ? "已解除禁言" : "解禁失败，请确认机器人是否有群管理权限");
        return;
    }

    if (starts_with(rawMessage, "禁言")) {
        size_t atEnd = 0;
        long long target = extract_cq_at(rawMessage, &atEnd);
        long long minutes = 0;
        if (target > 0) {
            auto nums = extract_numbers(rawMessage.substr(atEnd));
            if (!nums.empty()) minutes = nums[0];
        } else {
            auto nums = extract_numbers(rawMessage);
            if (nums.size() >= 1) target = nums[0];
            if (nums.size() >= 2) minutes = nums[1];
        }
        if (target <= 0 || minutes <= 0) {
            reply_group(accountId, groupId, "用法：禁言@某人 分钟");
            return;
        }
        if (minutes > 43200) minutes = 43200;
        int ok = g_api && g_api->set_group_ban_as
            ? g_api->set_group_ban_as(accountId.c_str(), groupId, target, (int)(minutes * 60))
            : 0;
        std::ostringstream ss;
        if (ok) ss << "已禁言 " << target << " " << minutes << " 分钟";
        else ss << "禁言失败，请确认机器人是否有群管理权限";
        reply_group(accountId, groupId, ss.str());
    }
}

static void worker_loop() {
    while (true) {
        CommandJob job;
        {
            std::unique_lock<std::mutex> lock(g_jobMutex);
            g_jobCv.wait(lock, [] { return !g_workerRunning.load() || !g_jobs.empty(); });
            if (!g_workerRunning.load()) break;
            job = std::move(g_jobs.front());
            g_jobs.pop_front();
        }
        try {
            handle_command(job.accountId, job.groupId, job.rawMessage);
        } catch (...) {
            log_line("error", "群管命令处理异常");
        }
    }
}

static bool is_command(const std::string& raw) {
    return raw == "全体禁言" || raw == "全体解禁" || raw == "禁言列表" || raw == "一键解禁" ||
           starts_with(raw, "禁言") || starts_with(raw, "解禁");
}

static bool is_owner(long long senderId) {
    if (senderId <= 0) return false;
    std::string sender = std::to_string(senderId);
    std::lock_guard<std::mutex> lock(g_ownerMutex);
    return std::find(g_ownerCache.begin(), g_ownerCache.end(), sender) != g_ownerCache.end();
}

static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    enum { ID_EDIT = 101, ID_SAVE = 102, ID_CANCEL = 103 };
    if (msg == WM_CREATE) {
        HFONT font = CreateFontW(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)font);
        CreateWindowW(L"STATIC", L"主人 QQ（每行一个，也支持逗号分隔）", WS_CHILD | WS_VISIBLE,
            22, 20, 340, 24, hwnd, nullptr, g_instance, nullptr);
        HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", utf8_to_wide(owners_to_edit_text()).c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL,
            22, 52, 420, 190, hwnd, (HMENU)ID_EDIT, g_instance, nullptr);
        CreateWindowW(L"STATIC", L"只有主人 QQ 发送群管指令时才会执行。配置保存在插件自己的 data 目录。", WS_CHILD | WS_VISIBLE,
            22, 252, 430, 24, hwnd, nullptr, g_instance, nullptr);
        HWND save = CreateWindowW(L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            252, 292, 90, 34, hwnd, (HMENU)ID_SAVE, g_instance, nullptr);
        HWND cancel = CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            352, 292, 90, 34, hwnd, (HMENU)ID_CANCEL, g_instance, nullptr);
        SendMessageW(edit, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(save, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageW(cancel, WM_SETFONT, (WPARAM)font, TRUE);
        return 0;
    }
    if (msg == WM_COMMAND) {
        int id = LOWORD(wp);
        if (id == ID_SAVE) {
            HWND edit = GetDlgItem(hwnd, ID_EDIT);
            int len = GetWindowTextLengthW(edit);
            std::wstring text((size_t)len + 1, L'\0');
            GetWindowTextW(edit, text.data(), len + 1);
            text.resize((size_t)len);
            bool ok = save_owners(parse_owner_list_text(wide_to_utf8(text)));
            MessageBoxW(hwnd, ok ? L"主人 QQ 已保存" : L"保存失败，请检查插件数据目录权限",
                L"Yuex 群管设置", ok ? MB_ICONINFORMATION : MB_ICONERROR);
            if (ok) log_line("info", "群管插件主人 QQ 配置已保存");
            return 0;
        }
        if (id == ID_CANCEL) {
            DestroyWindow(hwnd);
            return 0;
        }
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(hwnd);
        return 0;
    }
    if (msg == WM_DESTROY) {
        HFONT font = (HFONT)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (font) DeleteObject(font);
        g_settingsWindow = nullptr;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static DWORD WINAPI SettingsThreadProc(LPVOID) {
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        ShowWindow(g_settingsWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_settingsWindow);
        return 0;
    }
    WNDCLASSW wc = {};
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = g_instance;
    wc.lpszClassName = L"YuexGroupAdminSettingsWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Yuex 群管设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 480, 380, nullptr, nullptr, g_instance, nullptr);
    g_settingsWindow = hwnd;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = hinst;
        DisableThreadLibraryCalls(hinst);
    }
    return TRUE;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_get_info(YuexPluginInfo* info) {
    if (!info) return 0;
    info->abi_version = YUEX_PLUGIN_ABI_VERSION;
    info->id = kPluginId;
    info->name = "Yuex 群管";
    info->version = "1.0.1";
    info->author = "YuexBot";
    info->description = "Owner-only group mute/unmute commands for OneBot 11.";
    return 1;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_init(const YuexBotApi* api) {
    g_api = api;
    if (!g_api || g_api->abi_version < YUEX_PLUGIN_ABI_VERSION) return 0;
    const char* required[] = {"events", "onebot_api", "send_message", "data_dir"};
    for (const char* p : required) {
        if (g_api->has_plugin_permission && !g_api->has_plugin_permission(kPluginId, p)) {
            log_line("error", std::string("群管插件缺少权限: ") + p);
            return 0;
        }
    }
    if (g_api->get_data_dir) {
        const char* dir = g_api->get_data_dir(kPluginId);
        g_dataDir = dir ? dir : "";
    }
    if (g_dataDir.empty()) {
        log_line("error", "群管插件无法获取数据目录");
        return 0;
    }
    if (read_text_file(config_path()).empty()) save_owners({});
    refresh_owner_cache();
    bool ownersEmpty = false;
    {
        std::lock_guard<std::mutex> lock(g_ownerMutex);
        ownersEmpty = g_ownerCache.empty();
    }
    if (ownersEmpty) {
        log_line("warning", "群管插件已启用，但尚未配置主人 QQ，请在插件设置中添加");
    } else {
        log_line("info", "群管插件已启用");
    }
    if (!g_workerRunning.exchange(true)) {
        g_worker = std::thread(worker_loop);
    }
    return 1;
}

YUEX_PLUGIN_EXPORT void YUEX_PLUGIN_CALL yuex_plugin_shutdown() {
    if (g_settingsWindow && IsWindow(g_settingsWindow)) {
        PostMessageW(g_settingsWindow, WM_CLOSE, 0, 0);
    }
    g_workerRunning = false;
    {
        std::lock_guard<std::mutex> lock(g_jobMutex);
        g_jobs.clear();
    }
    g_jobCv.notify_all();
    if (g_worker.joinable()) g_worker.join();
    log_line("info", "群管插件已卸载");
    g_api = nullptr;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_on_event(int event_type, const char* event_json) {
    if (!g_api || event_type != YUEX_EVENT_MESSAGE || !event_json) return 0;
    std::string messageType = json_string_field(event_json, "message_type");
    long long groupId = json_i64_field(event_json, "group_id");
    if (messageType != "group" && groupId <= 0) return 0;
    std::string raw = trim_copy(json_string_field(event_json, "raw_message"));
    if (raw.empty()) raw = trim_copy(json_string_field(event_json, "message"));
    if (!is_command(raw)) return 0;

    long long senderId = json_i64_field(event_json, "sender_id");
    if (senderId <= 0) senderId = json_i64_field(event_json, "user_id");
    std::string accountId = json_string_field(event_json, "account_id");
    if (accountId.empty()) accountId = json_string_field(event_json, "self_id");
    if (accountId.empty()) {
        long long selfId = json_i64_field(event_json, "self_id");
        if (selfId > 0) accountId = std::to_string(selfId);
    }

    if (!is_owner(senderId)) {
        log_line("warning", "已忽略非主人群管指令，sender=" + std::to_string(senderId));
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(g_jobMutex);
        if (g_jobs.size() >= 512) g_jobs.pop_front();
        g_jobs.push_back(CommandJob{accountId, groupId, raw});
    }
    g_jobCv.notify_one();
    return 1;
}

YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_open_settings() {
    if (!g_api || g_dataDir.empty()) {
        MessageBoxW(nullptr, L"请先启用插件后再打开设置。", L"Yuex 群管设置", MB_ICONWARNING);
        return 0;
    }
    HANDLE th = CreateThread(nullptr, 0, SettingsThreadProc, nullptr, 0, nullptr);
    if (!th) return 0;
    CloseHandle(th);
    return 1;
}

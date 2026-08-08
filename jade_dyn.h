#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>

#ifndef JADEVIEW_CALL
#define JADEVIEW_CALL __stdcall
#endif

typedef struct WebViewSettings {
  int32_t autoplay; int32_t background_throttling; int32_t disable_right_click;
  const char *ua; const char *preload_js; int32_t allow_fullscreen;
  const char *postmessage_whitelist; const char *cors_whitelist;
} WebViewSettings;

typedef struct WebViewWindowOptions {
  const char *title; int32_t width; int32_t height; int32_t resizable;
  const char *frame_style; int32_t transparent; const char *background_color;
  int32_t always_on_top; const char *theme; int32_t maximized;
  int32_t maximizable; int32_t minimizable; int32_t x; int32_t y;
  int32_t min_width; int32_t min_height; int32_t max_width; int32_t max_height;
  int32_t fullscreen; int32_t focus; int32_t hide_window;
  int32_t use_page_icon; int32_t content_protection; int32_t auto_save_state;
} WebViewWindowOptions;

typedef const char *(*IpcCallback)(uint32_t, const char*);

#if defined(_WIN64)
typedef int32_t (JADEVIEW_CALL *pfn_JadeView_init)(int32_t, const char*, const char*, const char*, const char*, int32_t);
#else
typedef int32_t (JADEVIEW_CALL *pfn_JadeView_init)(int32_t, const char*, const char*);
#endif
typedef int32_t (JADEVIEW_CALL *pfn_run_message_loop)(void);
typedef uint32_t (JADEVIEW_CALL *pfn_create_webview_window)(const char*, uint32_t, const WebViewWindowOptions*, const WebViewSettings*);
typedef int32_t (JADEVIEW_CALL *pfn_navigate_to_url)(uint32_t, const char*);
typedef int32_t (JADEVIEW_CALL *pfn_execute_javascript)(uint32_t, const char*);
typedef int32_t (JADEVIEW_CALL *pfn_set_window_title)(uint32_t, const char*);
typedef int32_t (JADEVIEW_CALL *pfn_close_window)(uint32_t);
typedef int32_t (JADEVIEW_CALL *pfn_set_window_visible)(uint32_t, int32_t);
typedef uint32_t (JADEVIEW_CALL *pfn_jade_on)(const char*, IpcCallback);
typedef int32_t (JADEVIEW_CALL *pfn_register_ipc_handler)(const char*, IpcCallback);
typedef int32_t (JADEVIEW_CALL *pfn_set_protocol_service_path)(const char*, char*, size_t);
typedef int32_t (JADEVIEW_CALL *pfn_send_ipc_message)(uint32_t, const char*, const char*);
typedef int32_t (JADEVIEW_CALL *pfn_cleanup_all_windows)(void);
typedef char* (JADEVIEW_CALL *pfn_jade_text_create)(const char*);
typedef void (JADEVIEW_CALL *pfn_jade_text_free)(char*);
typedef int32_t (JADEVIEW_CALL *pfn_yaml_set)(const char*, const char*, const char*);
typedef int32_t (JADEVIEW_CALL *pfn_yaml_get)(const char*, const char*, char*, size_t);
typedef int32_t (JADEVIEW_CALL *pfn_getPath)(const char*, char*, size_t);

static HMODULE g_jadeDll = NULL;
static pfn_JadeView_init JadeView_init = NULL;
static pfn_run_message_loop run_message_loop = NULL;
static pfn_create_webview_window create_webview_window = NULL;
static pfn_navigate_to_url navigate_to_url = NULL;
static pfn_execute_javascript execute_javascript = NULL;
static pfn_set_window_title set_window_title = NULL;
static pfn_close_window close_window = NULL;
static pfn_set_window_visible set_window_visible = NULL;
static pfn_jade_on jade_on = NULL;
static pfn_register_ipc_handler register_ipc_handler = NULL;
static pfn_set_protocol_service_path set_protocol_service_path = NULL;
static pfn_send_ipc_message send_ipc_message = NULL;
static pfn_cleanup_all_windows cleanup_all_windows = NULL;
static pfn_jade_text_create jade_text_create = NULL;
static pfn_jade_text_free jade_text_free = NULL;
static pfn_yaml_set yaml_set = NULL;
static pfn_yaml_get yaml_get = NULL;
static pfn_getPath getPath_fn = NULL;
static const char* g_jadeDllName = "";
static DWORD g_jadeLastError = 0;
static const char* g_jadeMissingSymbol = "";

static bool load_jade_dll() {
    #if defined(_WIN64)
    const char* dllName = "JadeView_x64.dll";
    #else
    const char* dllName = "JadeView_x86.dll";
    #endif
    g_jadeDllName = dllName;
    g_jadeLastError = 0;
    g_jadeMissingSymbol = "";
    g_jadeDll = LoadLibraryA(dllName);
    if (!g_jadeDll) {
        g_jadeDll = LoadLibraryA((std::string("bin\\") + dllName).c_str());
    }
    if (!g_jadeDll) {
        g_jadeDll = LoadLibraryA((std::string("main\\bin\\") + dllName).c_str());
    }
    if (!g_jadeDll) { g_jadeLastError = GetLastError(); printf("LoadLibrary failed: %lu\n", g_jadeLastError); return false; }
    #define LOAD(name) name = (pfn_##name)GetProcAddress(g_jadeDll, #name); if (!name) { g_jadeMissingSymbol = #name; printf("Missing: %s\n", #name); return false; }
    LOAD(JadeView_init); LOAD(run_message_loop); LOAD(create_webview_window);
    LOAD(navigate_to_url); LOAD(execute_javascript); LOAD(set_window_title);
    LOAD(close_window); LOAD(set_window_visible); LOAD(jade_on);
    LOAD(register_ipc_handler);
    LOAD(send_ipc_message); LOAD(cleanup_all_windows);
    LOAD(jade_text_create); LOAD(jade_text_free);
    #undef LOAD
    set_protocol_service_path = (pfn_set_protocol_service_path)GetProcAddress(g_jadeDll, "set_protocol_service_path");
    yaml_set = (pfn_yaml_set)GetProcAddress(g_jadeDll, "yaml_set");
    yaml_get = (pfn_yaml_get)GetProcAddress(g_jadeDll, "yaml_get");
    getPath_fn = (pfn_getPath)GetProcAddress(g_jadeDll, "getPath");
    printf("JadeView DLL loaded OK: %s\n", dllName);
    return true;
}

static void unload_jade_dll() {
    if (g_jadeDll) { FreeLibrary(g_jadeDll); g_jadeDll = NULL; }
}

# YuexBot Plugin SDK

YuexBot plugins are Windows DLL files placed in `main/plugin`.

## XiaoLiZi / XLZ x86 compatibility status

YuexBot native plugins are still the recommended plugin format. XiaoLiZi/XLZ x86 DLLs, including DLLs built by 易语言 against the XLZ SDK, are recognized by the framework but cannot be loaded directly by the x64 YuexBot process.

Current phase:

- The framework detects x86 DLL load failures and keeps them in the plugin list as `小栗子 x86` bridge plugins.
- `YuexPluginHost32.exe` is a 32-bit out-of-process bridge host. It loads standard XiaoLiZi `apprun` SDK DLLs without placing them inside the x64 YuexBot process.
- Standard XiaoLiZi `apprun` plugins now support load, enable, settings command forwarding, shutdown, group/private/event dispatch, plugin log forwarding, and basic OneBot API request forwarding.
- DLLs without an `apprun` export are also supported when they use the 易语言 XLZ AppStart export set (`_AppStart`, `_AppEnd`, `_OnGroup`, `_OnPrivate`, `_OnEvent`, `_ControlPanel`) or the `Init` + `onGroupMsg` export set. Other unknown export sets are reported as `generic-x86` and need a separate adapter.

### Plugin metadata (author / version) for 易语言 plugins

易语言 / 小栗子 AppStart plugins set their app name, author and version by calling SDK routines (`置应用名`/`置应用作者`/`置应用版本`/`置应用说明`) that store the values inside the statically-linked SDK. These values are **not** exported in a way the host can read back, and 易语言-compiled DLLs usually ship with an empty PE version resource. As a result the host cannot auto-detect author/version for those plugins.

To make author and version show up in YuexBot, place a small UTF-8 JSON sidecar next to the plugin DLL. The host checks, in order: `<name>.info.json`, `<name>.meta.json`, then `<name>.json`.

```jsonc
// plugin/月溪Sky.info.json  (next to 月溪Sky.dll)
{
  "name": "月溪Sky",
  "author": "月溪",
  "version": "5.1.7",
  "sdkv": "5.1.7",
  "description": "小栗子易语言示例插件"
}
```

Metadata resolution priority used by `YuexPluginHost32.exe`:

1. Values the plugin pushes via SetApp* callbacks (used by C++ plugins that opt in).
2. The sidecar JSON file described above.
3. The PE version resource (`FileVersion`, `ProductName`, `CompanyName`, `FileDescription`) — works for C++ DLLs that embed one, such as `apprun` plugins.
4. The DLL file name as a last resort.

Build requirement for the x86 host:

```powershell
# Use a real 32-bit (i686) MinGW g++, not -m32 on an x64-only toolchain.
# version.lib (-lversion) is required for reading PE version metadata.
& "<i686-mingw>\bin\g++.exe" -O2 -std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8 -municode `
  -I. -I..\third_party xlz_x86_host.cpp `
  -static -static-libgcc -static-libstdc++ -lws2_32 -lshell32 -lversion `
  -o main\bin\YuexPluginHost32.exe
```

If the compiler reports that `-m32` is unsupported, install or provide a real i686/x86 Windows compiler first.

## Required exports

```cpp
YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_get_info(YuexPluginInfo* info);
YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_init(const YuexBotApi* api);
YUEX_PLUGIN_EXPORT void YUEX_PLUGIN_CALL yuex_plugin_shutdown();
YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_on_event(int event_type, const char* event_json);
```

Optional settings window export:

```cpp
YUEX_PLUGIN_EXPORT int YUEX_PLUGIN_CALL yuex_plugin_open_settings();
```

When this function exists, YuexBot shows the plugin settings button and calls it with the plugin
permission context active. Plugins can safely use `get_data_dir(plugin_id)` inside the settings
window to store per-plugin data.

The loader also accepts compatibility names:

```cpp
plugin_get_info / plugin_init / plugin_shutdown / plugin_on_event
XiaoLiZiPluginGetInfo / XiaoLiZiPluginInit / XiaoLiZiPluginShutdown / XiaoLiZiPluginOnEvent
```

## SDK API

All JSON return values are UTF-8 strings owned by YuexBot until the next SDK call on the same thread.

Common calls:

- `call_onebot_api(action, params_json)`
- `call_onebot_api_as(account_id_or_qq, action, params_json)`
- `call_onebot_api_ex(action, params_json)`
- `call_onebot_api_as_ex(account_id_or_qq, action, params_json)`
- `get_sdk_info()`
- `get_framework_version()`
- `get_active_account()`
- `get_accounts()`
- `get_account_status(account_id_or_qq)`
- `get_login_info()`
- `get_login_info_as(account_id_or_qq)`
- `get_status()`
- `get_status_as(account_id_or_qq)`
- `get_friend_list()`
- `get_friend_list_as(account_id_or_qq)`
- `delete_friend(user_id)`
- `get_group_list()`
- `get_group_list_as(account_id_or_qq)`
- `get_group_info(group_id)`
- `get_group_info_as(account_id_or_qq, group_id)`
- `get_group_member_list(group_id)`
- `get_group_member_list_as(account_id_or_qq, group_id)`
- `get_group_member_info(group_id, user_id)`
- `get_group_member_info_as(account_id_or_qq, group_id, user_id)`
- `send_msg(msg_type, target_id, message)`, where `msg_type` is `0` for private and `1` for group
- `send_msg_as(account_id_or_qq, msg_type, target_id, message)`
- `send_message` compatibility action through `call_onebot_api*`; use `message_type`/`type` = `group` or include `group_id` for group messages, otherwise include `user_id` for private messages.
- `send_private_msg(user_id, message)`
- `send_private_msg_as(account_id_or_qq, user_id, message)`
- `send_group_msg(group_id, message)`
- `send_group_msg_as(account_id_or_qq, group_id, message)`
- `delete_msg(message_id)`
- `delete_msg_as(account_id_or_qq, message_id)`
- `get_msg(message_id)`
- `get_msg_as(account_id_or_qq, message_id)`
- `get_forward_msg(id)`
- `get_forward_msg_as(account_id_or_qq, id)`
- `set_group_ban(group_id, user_id, duration)`
- `set_group_ban_as(account_id_or_qq, group_id, user_id, duration)`
- `set_group_kick(group_id, user_id, reject_add_request)`
- `set_group_kick_as(account_id_or_qq, group_id, user_id, reject_add_request)`
- `set_group_admin(group_id, user_id, enable)`
- `set_group_admin_as(account_id_or_qq, group_id, user_id, enable)`
- `set_group_name(group_id, group_name)`
- `set_group_name_as(account_id_or_qq, group_id, group_name)`
- `set_group_whole_ban(group_id, enable)`
- `set_group_whole_ban_as(account_id_or_qq, group_id, enable)`
- `set_group_leave(group_id, is_dismiss)`
- `set_group_leave_as(account_id_or_qq, group_id, is_dismiss)`
- `set_group_card(group_id, user_id, card)`
- `set_group_card_as(account_id_or_qq, group_id, user_id, card)`
- `set_group_special_title(group_id, user_id, special_title)`
- `set_group_special_title_as(account_id_or_qq, group_id, user_id, special_title)`
- `set_friend_add_request(flag, approve, remark)`
- `set_friend_add_request_as(account_id_or_qq, flag, approve, remark)`
- `set_group_add_request(flag, sub_type, approve, reason)`
- `set_group_add_request_as(account_id_or_qq, flag, sub_type, approve, reason)`
- `get_group_root_files(group_id)`
- `get_group_root_files_as(account_id_or_qq, group_id)`
- `get_group_files(group_id, folder_id, start_index)`
- `get_group_files_as(account_id_or_qq, group_id, folder_id, start_index)`
- `upload_group_file(group_id, file, name)`
- `upload_group_file_as(account_id_or_qq, group_id, file, name)`
- `delete_group_file(group_id, file_id, busid)`
- `delete_group_file_as(account_id_or_qq, group_id, file_id, busid)`
- `move_group_file(group_id, file_id, parent_folder, target_folder)`
- `move_group_file_as(account_id_or_qq, group_id, file_id, parent_folder, target_folder)`
- `create_group_folder(group_id, folder_name)`
- `create_group_folder_as(account_id_or_qq, group_id, folder_name)`
- `delete_group_folder(group_id, folder_id)`
- `delete_group_folder_as(account_id_or_qq, group_id, folder_id)`
- `rename_group_folder(group_id, folder_id, new_folder_name)`
- `rename_group_folder_as(account_id_or_qq, group_id, folder_id, new_folder_name)`
- `set_avatar(file)`
- `set_nickname(nickname)`
- `set_bio(bio)`
- `send_like(user_id, times)`
- `send_like_as(account_id_or_qq, user_id, times)`
- `send_group_notice(group_id, content, image)`
- `send_group_notice_as(account_id_or_qq, group_id, content, image)`
- `get_group_notice(group_id)`
- `get_group_notice_as(account_id_or_qq, group_id)`
- `get_custom_face_url_list()`
- `get_custom_face_url_list_as(account_id_or_qq)`
- `get_group_essence_msg_list(group_id)`
- `get_group_essence_msg_list_as(account_id_or_qq, group_id)`
- `get_plugin_config(plugin_id, key, default_value)`
- `set_plugin_config(plugin_id, key, value)`
- `get_data_dir(plugin_id)`
- `get_avatar_url(user_id, size)`
- `get_last_result()`
- `get_last_error()`
- `get_plugin_permissions(plugin_id)`
- `has_plugin_permission(plugin_id, permission)`
- `set_event_filter(plugin_id, event_mask)` compatibility no-op; YuexBot now dispatches all events.
- `get_event_filter(plugin_id)` returns the all-events mask.
- `ocr_image_as(account_id_or_qq, image)`
- `get_rkey_as(account_id_or_qq)`
- `get_clientkey_as(account_id_or_qq)`
- `get_group_album_list_as(account_id_or_qq, group_id)`
- `get_group_album_media_list_as(account_id_or_qq, group_id, album_id)`
- `get_group_system_msg_as(account_id_or_qq)`
- `get_group_ignore_add_request_as(account_id_or_qq)`
- `get_group_file_url_as(account_id_or_qq, group_id, file_id, busid)`
- `download_file_as(account_id_or_qq, url, headers_json)`
- `get_ai_characters_as(account_id_or_qq, group_id, chat_type)`
- `send_group_ai_record_as(account_id_or_qq, group_id, character, text)`
- `get_version_info_as(account_id_or_qq)`
- `get_stranger_info_as(account_id_or_qq, user_id, no_cache)`
- `get_group_honor_info_as(account_id_or_qq, group_id, type)`
- `get_record_as(account_id_or_qq, file, out_format)`
- `get_image_as(account_id_or_qq, file)`
- `can_send_image_as(account_id_or_qq)`
- `can_send_record_as(account_id_or_qq)`
- `get_cookies_as(account_id_or_qq, domain)`
- `get_csrf_token_as(account_id_or_qq)`
- `get_credentials_as(account_id_or_qq, domain)`
- `get_group_shut_list_as(account_id_or_qq, group_id)`
- `get_group_at_all_remain_as(account_id_or_qq, group_id)`
- `set_group_portrait_as(account_id_or_qq, group_id, file, cache)`
- `upload_private_file_as(account_id_or_qq, user_id, file, name)`
- `fetch_custom_face_as(account_id_or_qq, count)`
- `log(level, message)`

`call_onebot_api` and `call_onebot_api_as` return the native OneBot response directly. `call_onebot_api_ex`
and `call_onebot_api_as_ex` return YuexBot's normalized response:

```json
{
  "ok": true,
  "code": 0,
  "message": "ok",
  "action": "get_login_info",
  "data": {},
  "raw": {}
}
```

`get_sdk_info()` includes `onebot11_actions`, a catalog of supported standard OneBot 11 actions and
common NapCat/LLBot extensions. For APIs that do not have a typed SDK helper yet, call them through
`call_onebot_api_as_ex(account_id, action, params_json)`.

For a fuller coverage table, see `sdk/API_MATRIX.md`. It marks actions as:

- `typed`: dedicated C ABI helper exists.
- `generic`: use `call_onebot_api_as_ex`.
- `partial`: backend/plugin dispatch exists, but rich UI or typed wrappers are incomplete.
- `pending`: not verified yet.

Example:

```cpp
const char* r = api->call_onebot_api_as_ex(
    account_id,
    "get_stranger_info",
    "{\"user_id\":123456789,\"no_cache\":false}"
);
```

`account_id_or_qq` accepts an account id, QQ number, `active`, `current`, or `*`. YuexBot first tries
the target account's own runtime connection, then falls back to the active global connection when
applicable. Cached friend/group lists are returned when live transport is unavailable.

For multi-account plugins, prefer the `_as` APIs and pass the `account_id` from `event_json`.
For example, use `send_group_msg_as(account_id, group_id, message)` instead of the global
`send_group_msg(group_id, message)` when replying to an incoming group message.

## ABI v10 helpers

ABI v10 promotes several high-use OneBot 11 APIs from generic calls to typed account-aware helpers:

```cpp
api->get_version_info_as(account_id);
api->get_stranger_info_as(account_id, user_id, 0);
api->get_group_honor_info_as(account_id, group_id, "all");
api->get_record_as(account_id, file, "mp3");
api->get_image_as(account_id, file);
api->can_send_image_as(account_id);
api->can_send_record_as(account_id);
api->get_cookies_as(account_id, "qq.com");
api->get_csrf_token_as(account_id);
api->get_credentials_as(account_id, "qq.com");
api->get_group_shut_list_as(account_id, group_id);
api->get_group_at_all_remain_as(account_id, group_id);
api->set_group_portrait_as(account_id, group_id, file, 0);
api->upload_private_file_as(account_id, user_id, file, name);
api->fetch_custom_face_as(account_id, 48);
```

These helpers still return the native implementation response, matching the behavior of other `_as` API wrappers.

## ABI v9 helpers

`get_last_result()` returns the last normalized SDK result JSON for APIs that support result
tracking. `get_last_error()` returns only the latest failure message, which is convenient after
boolean APIs return `0`.

Permissions are stored by the framework in `main/data/plugins/<plugin_id>.json`:

```json
{
  "permissions": ["events", "onebot_api", "send_message", "config", "data_dir"]
}
```

Use `get_plugin_permissions(plugin_id)` or `has_plugin_permission(plugin_id, permission)` to inspect
the declared permissions. ABI v9 applies basic permission checks to OneBot raw calls, send-message
helpers, plugin config, and plugin data-directory helpers. Plugins should still call
`has_plugin_permission` during initialization and fail early with a clear log when required
permissions are missing.

YuexBot now dispatches all OneBot messages and events to enabled plugins by default. Event filtering
is no longer exposed in the UI because it can make plugins appear unresponsive when users disable an
event type by mistake. The `set_event_filter` and `get_event_filter` ABI entries are kept only for
old plugin compatibility; new plugins should simply return early from `yuex_plugin_on_event` when an
event type is not relevant.

## Event types

- `YUEX_EVENT_MESSAGE`
- `YUEX_EVENT_NOTICE`
- `YUEX_EVENT_REQUEST`
- `YUEX_EVENT_META`

`event_json` contains the original OneBot event JSON plus YuexBot-normalized fields.

YuexBot dispatches plugin events through a background queue so plugin callbacks do not block the
main message parser or UI log rendering. Plugins receive messages, notices, requests, and meta
events by default. Plugin callbacks should still return quickly; move long-running work to a
plugin-owned worker thread when possible, and avoid repeated blocking API calls inside every message
event.

For command-style plugins, use this pattern:

1. In `yuex_plugin_on_event`, only do fast checks such as event type, command keyword, and permission.
2. Push the real work into a plugin-owned queue.
3. Let the plugin worker call OneBot APIs and send replies.
4. Cache plugin config in memory and update the cache from the settings window instead of reading
   files for every message.

The `yuex_group_admin.cpp` example follows this pattern. It keeps owner QQs in memory and executes
mute/unmute API calls from a plugin worker thread so message callbacks return immediately.

Normalized fields added by YuexBot ABI v7:

- `account_id`, `account_name`
- `self_id`
- `sender_id`, `sender_name`
- `group_id`, `group_name`
- `message_id`
- `target_id`, `operator_id`
- `post_type`, `message_type`, `notice_type`, `request_type`, `sub_type`

Plugins should prefer these normalized fields for account-aware logic, while keeping
raw OneBot fields for protocol-specific details.

## Build example

```powershell
g++.exe -O2 -std=c++17 -I. plugin\echo_sample.cpp -shared -static "-Wl,--out-implib,plugin\echo_sample.a" -o plugin\echo_sample.dll
```

## Template

Use `sdk/plugin_template.cpp` and `sdk/plugin_template.json` as a starting point for new native
plugins. Copy the `.cpp` into `plugin/`, rename `kPluginId`, plugin metadata, and command logic,
then copy the `.json` to `plugin/<plugin_id>.json`.

Example:

```powershell
copy sdk\plugin_template.cpp plugin\my_yuex_plugin.cpp
copy sdk\plugin_template.json plugin\my_yuex_plugin.json
g++.exe -O2 -std=c++17 -I. -shared -static plugin\my_yuex_plugin.cpp -o plugin\my_yuex_plugin.dll
```

## More examples

See `sdk/examples/` for additional native SDK examples:

- `message_reply_example.cpp`: multi-account `/ping` reply.
- `config_permission_example.cpp`: plugin permissions, config, and data directory.
- `onebot_api_example.cpp`: normalized OneBot API calls.
- `extension_api_example.cpp`: NapCat/LLBot extension calls through the generic API bridge.
- `yuex_group_admin.cpp`: owner-only group mute/unmute commands and a Win32 settings window.

The plugin manager also provides a standard configuration dialog for native plugins. It displays
declared permissions and confirms that enabled plugins receive all messages and events by default.
When `main/plugin/<plugin_id>.json` exists and `main/data/plugins/<plugin_id>.json` does not exist
yet, YuexBot imports the manifest automatically on first scan.

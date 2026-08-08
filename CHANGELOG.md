# YuexBot Changelog

## v1.0.1

- Added 易语言/小栗子 XLZ plugin metadata resolution: SetApp* host callbacks, `<name>.info.json` sidecar, and PE version resource, with filename fallback.
- Added `-lversion` to the 32-bit host build and removed dead `json`-dependent code that broke host compilation.
- Documented the plugin metadata sidecar in `sdk/README.md`, `sdk/开发指南.md`, and added `sdk/plugin_metadata.info.json` template.
- Upgraded native plugin SDK to ABI v9.
- Added SDK `get_last_result`, `get_last_error`, plugin permission inspection, and event filters.
- Added basic runtime permission checks for plugin OneBot/config/data directory/send-message calls.
- Added OneBot 11 action catalog to `get_sdk_info()`.
- Added native plugin template files under `sdk/`.
- Improved plugin event dispatch to avoid holding the plugin mutex during callbacks.
- Added friend detail, like, and delete actions in the UI.
- Added log detail, copy, and raw event JSON views in message/log pages.
- Added filtered log export in dashboard, message center, and log center.
- Added clearer NapCat/LLBot API compatibility hints for friend operations.
- Added Chinese native plugin SDK guide under `sdk/`.
- Added message-center filters for group id/name and sender id/name.
- Added account-card diagnostics for event channel, API channel, group/friend counts, latency, and latest connection error.
- Added plugin permission badges in the plugin manager.
- Added loading states for friend and group list refresh.
- Added standard plugin configuration dialog with permission display and editable event filters.
- Added more native SDK examples under `sdk/examples`.
- Improved group/friend table column sizing and long-text titles.
- Added current active account display in the top subtitle and bottom status bar.
- Cleaned sample plugin metadata/log text to avoid mojibake in plugin manager and logs.
- Improved dashboard stat-card wrapping for normal desktop window sizes.
- Removed duplicate group/friend empty states and kept clearer in-table loading/empty text.
- Added SDK API coverage matrix for typed/generic/partial/pending NapCat, LLBot, and OneBot APIs.
- Added a native extension API example for calling NapCat/LLBot actions through `call_onebot_api_as_ex`.
- Expanded `get_sdk_info()` with typed action, generic extension action, event support, and message segment coverage metadata.
- Added typed SDK wrappers for OCR, RKey/clientkey, group albums, group system messages, ignored add requests, group file URLs/downloads, and AI voice helpers.
- Refined dashboard overview into 3 business cards plus one compact runtime status card.
- Changed top connection state to multi-account summary instead of one global connected/disconnected label.
- Improved log message previews with small media cards for image/voice/video/file segments.
- Added startup trace log rotation.
- Added plugin runtime diagnostics in the plugin manager: event count, average/max callback time, slow callback count, and recent errors.
- Increased frontend log retention and added virtualized log table rendering to reduce repaint cost while keeping long history.
- Added optional service-sync settings and a non-blocking service connectivity probe for update/telemetry/plugin-security integration.

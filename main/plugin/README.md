# YuexBot Plugins

Drop plugin DLLs here.

Required exports:
- `yuex_plugin_get_info`
- `yuex_plugin_init`
- `yuex_plugin_shutdown`
- `yuex_plugin_on_event`

Use `sdk/yuex_plugin_sdk.h`. ABI v7 event JSON includes normalized fields such
as `account_id`, `account_name`, `sender_name`, `group_name`, and type fields.

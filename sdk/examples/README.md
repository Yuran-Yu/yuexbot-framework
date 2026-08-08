# YuexBot SDK Examples

This directory contains native YuexBot plugin examples.

## Examples

- `message_reply_example.cpp`: multi-account `/ping` reply.
- `config_permission_example.cpp`: permission checks, plugin config, and data directory usage.
- `onebot_api_example.cpp`: normalized OneBot API call with `call_onebot_api_as_ex`.
- `extension_api_example.cpp`: generic NapCat/LLBot extension API call pattern.
- `yuex_group_admin.cpp`: owner-only group mute/unmute plugin with a native settings window.
- `example_plugin.json`: manifest template with permissions and event mask.
- `yuex_group_admin.json`: manifest for the group admin plugin.
- `xlz_e_language_plugin.info.json`: sidecar metadata template for 易语言 / 小栗子 AppStart DLLs.

## Build

Run from the `main` directory:

```powershell
D:\易语言\web开发\mingw64\bin\g++.exe -O2 -std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8 -Isdk -shared -static sdk\examples\message_reply_example.cpp -o plugin\message_reply_example.dll
copy sdk\examples\example_plugin.json plugin\message_reply_example.json
```

The main YuexBot executable is x64, so native plugins must also be x64.

For 易语言 / 小栗子 AppStart plugins, copy `xlz_e_language_plugin.info.json` next to the DLL and rename it to match the DLL stem, for example `月溪Sky.info.json` next to `月溪Sky.dll`. This file is only for metadata display; native YuexBot plugins should keep using `yuex_plugin_get_info`.

Build the group admin plugin:

```powershell
g++.exe -O2 -std=c++17 -finput-charset=UTF-8 -fexec-charset=UTF-8 -Isdk -shared -static sdk\examples\yuex_group_admin.cpp -o plugin\yuex_group_admin.dll -luser32 -lgdi32
copy sdk\examples\yuex_group_admin.json plugin\yuex_group_admin.json
```

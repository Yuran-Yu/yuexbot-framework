# YuexBot Service

YuexBot Service is an isolated backend for update checks, framework telemetry, account statistics,
plugin inventory, and plugin compliance checks.

## Native EXE

Preferred desktop build:

```text
release\YuexBotService-v0.1.0\YuexBotService.exe
```

Native package:

```text
release\YuexBotService-v0.1.0-native.zip
```

The native version is a standalone C++/JadeView desktop app with the same light YuexBot tool style.

Build it with:

```powershell
.\build-native-service.ps1
```

## Node Prototype

```powershell
npm start
```

Default URL:

```text
http://127.0.0.1:8787
```

Windows helper scripts:

```powershell
.\start-service.ps1
.\stop-service.ps1
```

Optional environment variables:

```text
YUEXBOT_SERVICE_PORT=8787
YUEXBOT_ADMIN_TOKEN=change-me
```

## API

```text
GET  /api/health
POST /api/v1/update/check
POST /api/v1/telemetry/framework
POST /api/v1/telemetry/accounts
POST /api/v1/telemetry/plugins
GET  /api/admin/summary
GET  /api/admin/events
GET  /api/admin/plugins
```

## Update Check

Request:

```json
{
  "version": "1.1.0",
  "channel": "stable",
  "install_id": "local-generated-id"
}
```

Response:

```json
{
  "ok": true,
  "update_available": false,
  "current_version": "1.1.0",
  "latest": {
    "version": "1.1.0",
    "url": "https://example.com/yuexbot/YuexBot-v1.1.0.zip",
    "sha256": "",
    "mandatory": false,
    "notes": []
  }
}
```

## Data Policy

The service stores account identifiers as hashes for telemetry views. Raw display names can still be
submitted by the client if the user enables that option later. The desktop client should expose a
clear switch before sending telemetry to a public server.

## Plugin Compliance

Rules live in:

```text
config/plugin-rules.json
```

Current rule types:

- `banned_ids`
- `banned_keywords`
- `suspicious_permissions`

The service returns every reported plugin with `compliance: clean` or `compliance: blocked`.

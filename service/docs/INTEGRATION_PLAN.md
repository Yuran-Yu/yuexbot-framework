# YuexBot Service Integration Plan

## Goal

Add update checks, telemetry, account statistics, plugin statistics, and plugin compliance without
making the desktop framework depend on the server at startup.

## No-Conflict Boundary

- The service is a standalone Node process in `jadebot/service`.
- YuexBot desktop keeps working when the service is offline.
- Desktop integration should be behind settings switches.
- Network calls should run in a background worker and never block WebSocket, OneBot API, plugin
  loading, or the UI thread.
- Server payloads use versioned paths under `/api/v1`.

## Desktop Client Additions

Recommended new files later:

```text
jadebot/service_client.h
jadebot/service_client.cpp
```

Recommended settings:

```json
{
  "service": {
    "enabled": false,
    "base_url": "http://127.0.0.1:8787",
    "channel": "stable",
    "telemetry": {
      "framework": true,
      "accounts": false,
      "plugins": true
    }
  }
}
```

## Reporting Flow

1. On startup, check `/api/v1/update/check`.
2. Every 5 minutes, send `/api/v1/telemetry/framework`.
3. When account state changes, send `/api/v1/telemetry/accounts` if enabled.
4. When plugins are loaded or reloaded, send `/api/v1/telemetry/plugins`.
5. If server returns blocked plugins, desktop UI marks them in plugin management before taking
   any destructive action.

## Privacy Defaults

Keep account telemetry off by default until the settings UI clearly explains what is sent.
Framework and plugin aggregate telemetry can be enabled first because it is less sensitive.

## Future Server Upgrades

- Replace JSON files with SQLite.
- Add admin login and audit logs.
- Add plugin signing and hash allowlist.
- Add release file upload and SHA256 verification.
- Add client-side backoff and offline queue.

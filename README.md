# YuexBot

YuexBot is a Windows desktop QQ bot framework built with C++ and JadeView, designed around OneBot 11 multi-account operation, plugin extensibility, and a modern light-theme desktop UI.

## Highlights

- Multi-account bot management for forward and reverse WebSocket connections
- OneBot 11 event receiving, message logging, account and plugin views
- Native YuexBot plugin SDK for x64 DLL plugins
- XLZ / XiaoLiZi x86 bridge-host compatibility layer for selected legacy plugins
- Built-in desktop service project for update checks, telemetry, and plugin compliance review
- JadeView-based UI with embedded HTML frontend resources

## Repository Layout

```text
main.cpp                 Desktop framework main entry
sdk/                     YuexBot plugin SDK, docs, templates, examples
main/UI/                 Runtime UI entry HTML
assets/logo/             Logo assets and preview
service/                 Update / telemetry / compliance service source
plugin/                  Source-side sample plugins and compatibility experiments
main/plugin/             Runtime plugin folder template
```

## Build Notes

This project targets Windows.

- Main framework: C++17
- UI runtime: JadeView
- 32-bit bridge host: separate x86 build for legacy XLZ-style plugins

Some runtime binaries and release packages are intentionally excluded from this repository. The repo focuses on source, SDK, UI assets, and service code.

## Plugin SDK

See [sdk/README.md](./sdk/README.md) and [sdk/API_MATRIX.md](./sdk/API_MATRIX.md).

## Service

See [service/README.md](./service/README.md).

## Release / Runtime Files

Local account data, logs, WebView caches, built binaries, and packaged release zips are excluded from version control.

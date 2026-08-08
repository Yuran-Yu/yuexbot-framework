# YuexBot Framework Optimization Notes

## High Value

- Regenerate `embedded_ui.h` whenever `www/index.html` changes, otherwise the packaged exe can show
  an older UI.
- Add a settings switch for update checks and telemetry before connecting the desktop framework to
  the service.
- Move service reporting into a small background client so it never blocks OneBot connections.
- Keep account telemetry disabled by default until the user can see exactly what is being sent.
- Add plugin compliance result display in plugin management after the service endpoint is connected.

## Medium Value

- Add a formal release manifest with `version`, `url`, `sha256`, `mandatory`, and `notes`.
- Add log rotation for exported and runtime logs.
- Add a UI indicator when a plugin is locally disabled because of service compliance rules.
- Add a one-click diagnostic export that excludes tokens by default.

## Later

- Move service data from JSON files to SQLite.
- Add signed plugin metadata.
- Add opt-in anonymous install id generation.
- Add admin authentication UI for the service console.

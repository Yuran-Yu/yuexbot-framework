# YuexBot 原生服务端

`YuexBotService.exe` 是独立桌面服务端程序，使用 C++ 和 JadeView 构建。它和主框架一样是 exe 窗口程序，默认提供蓝白轻量工具风 UI，并在本机启动 HTTP API。

## 运行

```text
release\YuexBotService-v0.1.0\YuexBotService.exe
```

默认地址：

```text
http://127.0.0.1:8787
```

## 已实现功能

- 更新检查：`POST /api/v1/update/check`
- 框架数据上报：`POST /api/v1/telemetry/framework`
- 账号数据上报：`POST /api/v1/telemetry/accounts`
- 插件数据上报和风险检测：`POST /api/v1/telemetry/plugins`
- 管理端概览：`GET /api/admin/summary`
- 上报事件流水：`GET /api/admin/events`
- 本地 JSON 数据持久化：`service_data`

## 和框架集成原则

- 服务端独立运行，不阻塞 YuexBot 主框架启动。
- 框架后续通过可选开关上报数据，服务端离线时框架继续正常使用。
- 插件风控先做提示和记录，后续可在框架插件管理页显示服务端检测结果。

## 构建

```powershell
.\build-native-service.ps1
```

输出目录：

```text
release\YuexBotService-v0.1.0
```

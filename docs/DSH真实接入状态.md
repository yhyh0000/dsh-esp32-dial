# DSH Guard 真实接入状态

## 已验证的板端固件路径

固件主入口 `firmware/main/main.cpp` 现在自动启动 `DshApp`，不再启动 `CompanionApp`。板端协议仍为：

```text
ESP32 DshApp -- WebSocket --> Local Bridge -- DSH Host RPC/Event stream --> DeepSeek Harness
```

板端收：`state`、`ask`、`pong`；板端发：`hello`、`ping`、`answer`。其中 `answer` 对应 Local Bridge 向 DSH 发送 `client-response`。

## 当前需要配置的板端连接

首次配网访问 `http://192.168.4.1/settings` 后，为当前 LAN 验证模式填写：

```text
Bridge Host: 192.168.10.2
Bridge Port: 3083
Device Token: 115287cedc57ab4ca80fcca3f5b4c4f6
```

这些值仅用于当前开发机上的临时 DSH Guard Bridge。远程 Gateway/MQTTS 配置是后续数据面，不能替代该 LAN 验证链路。

## DSH 当前版本的认证限制

当前 DeepSeek Harness 的 127.0.0.1:3080 不是旧式无认证 loopback RPC：Host RPC 与 event WebSocket 都需要通过 GUI 根路径启动令牌换取、并绑定 authority 的浏览器 session cookie。缺少 cookie 的 `session.list` 返回 HTTP 401。

这意味着旧的 `bridge.js` 直接 Node HTTP/WS 调用方式不能在当前 DSH 版本中作为生产 DSH Adapter 使用。它仍可保留为早期协议参考，但真实接入必须实现以下之一：

1. **DSH Client Plugin（推荐）**：在 DSH 运行时/客户端插件内订阅 Session 与审批事件，并把已授权的事件通过 localhost IPC 交给 Local Bridge；插件自身使用 DSH 的受信任 client context，不复制浏览器 cookie。
2. **受控 Browser Client Adapter**：由用户明确授权的浏览器会话通过官方 client protocol 提供桥接；不得抓取、导出或绕过浏览器 cookie。

禁止把启动 token、浏览器 session cookie 或 DSH 登录凭据复制到 ESP32、Gateway 或公开的配置页。

## 真正的验收标准

在实现 Client Plugin Adapter 后，必须逐项验证：

```text
DSH session/list or projection event → Local Bridge → ESP32 state
DSH approval/requested              → Local Bridge → ESP32 ask
ESP32 answer                         → Local Bridge → DSH client-response
断开 ESP32 / Bridge / DSH            → 明确 offline/stale，绝不默认批准
```

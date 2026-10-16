# Agent Satellite Gateway：Wi‑Fi 设备接入与切换设计

本设计面向 ESP32-S3-Touch-LCD-1.85B。设备**不使用蓝牙**，也不直连 DSH 主机；ESP32 与 Local Bridge 都通过 Wi‑Fi/互联网主动连接 VPS Gateway。

```text
ESP32 Satellite -- MQTTS --> Gateway/VPS <-- MQTTS -- Local Bridge -- loopback --> DSH/Codex
```

## 为什么需要 Gateway

- DSH 的本地 API 只允许 loopback 调用，ESP32 不应直接访问。
- 一块板可能切换控制多台电脑；一台电脑也可能服务多块板。
- Gateway 负责设备身份、Host 发现、Session 路由、审计和超时；它不执行 Shell，也不保存 DSH/Codex 登录凭据。
- 局域网、公司网络和异地网络均可工作，无需蓝牙、公网 IP 或端口映射。

## 三类对象

| 对象 | 例子 | 负责什么 |
|---|---|---|
| Device | `badge-185b-office` | 显示、触摸审批、语音输入、电量/在线状态 |
| Host | `host-windows-main` | 本机 DSH、Codex、VS Code/MCP 的适配与最终执行 |
| Session | `dsh-session-abc` | 一个可显示状态、接收输入或接收审批结果的 Agent 会话 |

Device、Host 与 Session 必须独立建模，不能由 ESP32 固件保存某台电脑 IP 或某个固定 Session ID。

## 设备能力卡

Device 上线时向 Gateway 声明能力；Gateway 仅把事件路由给能处理它的在线设备。

```json
{
  "type": "device.capabilities",
  "deviceId": "badge-185b-office",
  "kind": "esp32-s3-touch-lcd-1.85b",
  "capabilities": {
    "display": true,
    "touchApproval": true,
    "voiceInput": true,
    "audioOutput": false,
    "pixelUi": true
  }
}
```

这个模型借鉴 MQTT MCP/A2A 中的 Agent Card/Tool Schema：能力可被发现而不写死设备型号或命令主题。

## Route：当前设备连接目标

一个 Device 在任意时刻最多有一条 active route：

```json
{
  "routeId": "route_01J...",
  "deviceId": "badge-185b-office",
  "hostId": "host-windows-main",
  "agentType": "dsh",
  "sessionId": "session-abc",
  "role": "primary",
  "mode": "until_reboot",
  "expiresAt": "2026-10-15T16:00:00Z"
}
```

`role`：

| 角色 | 权限 |
|---|---|
| `primary` | 查看状态、发送输入、接收并一次性审批 |
| `observer` | 只读状态与告警确认，不能批准 |
| `alert` | 只接收 P0/P1 通知 |
| `kiosk` | 演示数据，不可连接真实 Host |

审批请求须明确 `assignedDeviceId`，其他设备只显示“已由其他设备处理”，不可作答。

## MQTT Topic 与最小 ACL

```text
agentguard/v1/{tenant}/device/{deviceId}/inbox
agentguard/v1/{tenant}/device/{deviceId}/outbox
agentguard/v1/{tenant}/device/{deviceId}/presence

agentguard/v1/{tenant}/host/{hostId}/inbox
agentguard/v1/{tenant}/host/{hostId}/outbox
agentguard/v1/{tenant}/host/{hostId}/presence
```

- Device：仅订阅自己的 `inbox`，仅发布自己的 `outbox/presence`。
- Host：仅订阅自己的 `inbox`，仅发布自己的 `outbox/presence`。
- Gateway：唯一拥有跨设备/Host 路由权的主体。
- 使用 MQTT 5 的 correlation data 或 `{requestId, nonce}` 关联请求/响应，不能把旧设备回应路由到新请求。

## 设备切换流程

```text
1. Device → Gateway: route.hosts.list
2. Gateway → Device: route.hosts
3. Device → Gateway: route.select_host(hostId)
4. Gateway → Host: route.agents.list
5. Host → Gateway: route.agents
6. Device → Gateway: route.select_agent(agentType)
7. Gateway → Host: route.sessions.list
8. Host → Gateway: route.sessions
9. Device → Gateway: route.activate(hostId, agentType, sessionId)
10. Gateway → Device + Host: route.active
```

在圆屏上显示为：

```text
SELECT HOST → SELECT AGENT → SELECT SESSION → ROUTE ACTIVE
```

第一版可由 Gateway Web 控制台选择 Route，再把只读 Route 下发给 ESP32；这样先验证路由与安全，再实现圆屏选择器。

## 配网与配对

首次启动不使用蓝牙：

```text
ESP32 SoftAP → 手机/电脑访问 192.168.4.1
  → 写入 Wi‑Fi、Gateway host、MQTTS port、Tenant、Device ID、Pair Credential
  → 重启连接 Gateway
```

ESP32 不保存：DSH Token、Codex Token、邮箱密码、Bridge IP、Host 私钥。

推荐后续升级到 mTLS：每台板有独立证书与 ACL；初期可使用独立设备凭据 + TLS 服务端证书校验。

## 失效策略

- Device/Host 通过 MQTT LWT + retained presence 报告在线状态。
- 设备 45 秒无心跳：标记 offline；不得接受审批。
- Host 离线：Route 变 `degraded`；设备展示最后同步时间。
- Approval TTL 到期：Gateway 发 `deny` 或 Host 回退原生 DSH UI；永不默认允许。
- Gateway 无法连接：ESP32 显示 `OFFLINE`，不显示旧数据为实时。

## 可借鉴技术

- [EMQX MCP Bridge](https://docs.emqx.com/en/emqx/latest/develop/emqx-ai/mcp-bridge/overview.html)：设备能力注册、MCP 到 MQTT 适配。
- [EMQX A2A over MQTT](https://docs.emqx.com/zh/emqx/latest/develop/emqx-ai/a2a-over-mqtt/overview.html)：Agent Card、在线发现与协作。
- [ESP RainMaker Local Control](https://docs.espressif.com/projects/esp-rainmaker-neo-firmware/en/latest/specs/local_ctrl_endpoint_protocol.html)：Node ID、能力、发现与挑战响应。
- [ThingsBoard Gateway API](https://thingsboard.io/docs/paas/eu/reference/gateway-api/overview/)：Telemetry、Attribute、RPC 的网关代理分层。
- [Home Assistant Assist Satellite](https://developers.home-assistant.io/docs/core/entity/assist-satellite/)：独立 Satellite 状态机和多设备体验。

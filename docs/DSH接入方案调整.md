# DSH 接入方案调整：复用已安装的 Mobile Gateway

## 结论

在开发新的 Browser Client Plugin Adapter 原型后，检查本机 DSH Profile 发现已经安装：

```text
dsh-plugin-mobile-gateway@0.10.0
```

它已经针对当前 DSH `0.2.0-rc.2` 实现了我们所需的受信任接入能力：设备配对、认证 WebSocket、会话列表与实时输出、用户消息、问题回答、一次性审批、会话取消、LAN 与 Cloudflare Tunnel 多地址。因此不应再以自定义浏览器插件作为主数据通道；应复用该 Gateway 作为 DSH 数据面，将 ESP32 Bridge 变为它的受限硬件客户端。

## 已核验的 Gateway 能力

来自本机已安装插件 `C:\Users\FY\.dsh\profiles\node_modules\dsh-plugin-mobile-gateway\PROTOCOL.md`：

- 每台设备通过一次性 256-bit pairing code 配对，随后使用长期独立设备 token；
- WebSocket 有 LAN (`ws://<LAN-IP>:3081/ws/mobile`) 与 TLS 公网 (`wss://.../ws/mobile`) 入口；
- `sessions`、`subscribe`、`event`：会话状态和实时 Agent 输出；
- `message`：向既有 Session 发送文字，或创建新 Session；
- `question-requested` / `question-answer`：`ask_user_question` 交互；
- `approval-requested` / `approval-response`：一次性 `allowed-once` / `rejected`；
- 多端单胜方：WebUI 或任一移动设备先完成，其他端收到 resolved 并收起；
- `session-cancel`、队列控制、任务、目标、上下文、历史、文件下载等。

这些能力来自 DSH 内部受信任 Host waterfall，而非通过 Node 抓取浏览器 cookie。

## ESP32 适配边界

ESP32 不实现完整手机协议，只实现必要子集：

```text
连接/配对：WebSocket + dsh-mobile-v1 子协议
接收：hello, event, approval-requested, question-requested, approval-resolved, question-resolved
发送：ping, subscribe, message, approval-response, question-answer, session-cancel
```

板端只展示脱敏的工具名、原因、任务摘要和文本摘要；不保存 DSH cookie、启动 token、电脑账号密码或完整敏感工具参数。

## 新架构

```text
ESP32 DSH Guard
  -- Wi-Fi WebSocket --> dsh-plugin-mobile-gateway
  -- authenticated / paired device token --> DSH Host
  -- optional TLS tunnel --> Cloudflare / reverse proxy

Local ESP32 Bridge
  -- optional only --> legacy LAN preview, Codex, pixel simulation
```

因此此前 `bridge.js` 的直接 DSH HTTP/Event Mux 代码只能作为旧协议参考或非 DSH 后端适配，不能作为当前 DSH 的认证数据面。

## 下一步实施顺序

1. 在 DSH Plugin Manager 中显式启用 `dsh-plugin-mobile-gateway`；不得通过代码绕过其安全开关。
2. 从 Gateway Web UI 生成一次性配对码/二维码并确认 LAN 或 TLS endpoint。
3. ESP32 固件新增 `mobile_gateway_transport`：首次配对保存设备 token 到 NVS，后续带 token 连接。
4. 先实现 `hello → sessions → subscribe → state/event` 的只读显示。
5. 再实现 `approval-requested → 物理 allow/deny → approval-response`。
6. 最后实现 PTT/ASR 产生的 `message`，不在板端处理 DSH 认证。

## 为什么仍保留 dsh-plugin 原型

`dsh-plugin/` 是对当前 DSH Client Remote Event waterfall 的最小安全学习原型；其 loopback relay 可用于对比测试，但不再是生产链路。生产应优先使用 Mobile Gateway，因为它已经实现多设备配对、请求重放、单胜方仲裁、WebUI 回退和当前 DSH 版本兼容。

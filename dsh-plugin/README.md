# DSH Satellite Browser Plugin Prototype

This directory contains the safe integration design for the current DSH browser-session model.

The installed DSH runtime requires its signed, authority-bound browser session cookie for every Host RPC and event stream. The ESP32 and Node Local Bridge must never receive that cookie. Therefore the integration is intentionally split:

```text
Authenticated DSH browser plugin
  ├─ ctx.remote.$on("approval/request", ...)
  ├─ POST localhost /plugin/approval/request
  ├─ GET localhost /plugin/approval/{id}/wait
  └─ return allowed-once / rejected to the original Remote waterfall

Local Bridge
  ├─ relays sanitized ask to ESP32
  └─ returns only a single transient decision
```

## Required plugin manifest

A production package must declare a browser bundle:

```json
{
  "name": "@suhmyh/dsh-esp32-satellite",
  "type": "module",
  "exports": { "./client": "./lib/client.js" },
  "dsh": {
    "client": {
      "platform": "web",
      "inject": [
        "@deepseek-ai/dsh-api-remotes",
        "@deepseek-ai/dsh-api-session-controller"
      ]
    }
  }
}
```

The repository now includes a minimal prebuilt `lib/client.js` bundle for the exact observed runtime shape (`0.2.0-rc.2`). It must run inside the authenticated Web client; a standalone Node process cannot access `ctx.remote` or the browser Remote Event waterfall.

> Install only through the DSH Plugin Manager with explicit human approval. The bundle is intentionally limited to the built-in `approval/request` Remote Event and falls back to DSH's normal approval UI on every relay error or timeout.

## Approval listener contract

The intended `client.ts` behavior is expressed as pseudocode below. It deliberately uses only transient outcomes, matching the built-in approval UI.

```ts
ctx.remote.$on("approval/request", async function (request, next) {
  const sessionId = ctx.sessions.scopeOf(this)
  if (!sessionId) return next()

  const created = await fetch("http://127.0.0.1:3083/plugin/approval/request", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({
      toolName: request.toolName,
      reason: request.reason,
      ttlMs: 60_000,
    }),
  })
  if (!created.ok) return next() // preserve DSH's built-in approval UI

  const { approvalId } = await created.json()
  const result = await fetch(`http://127.0.0.1:3083/plugin/approval/${approvalId}/wait`)
  if (result.status !== 200) return next() // timeout/offline → native UI
  const { outcome } = await result.json()
  return outcome === "allowed-once" ? "allowed-once" : "rejected"
})
```

## Safety rules

- The browser plugin only talks to `127.0.0.1`; it does not send DSH cookies, launch token, session data, or credentials to ESP32/Gateway.
- The bridge accepts plugin relay endpoints only from loopback source addresses.
- `approvalId` is bridge-generated, random, one-time and expires in at most 120 seconds.
- Only `allowed-once` and `rejected` can be returned; persistent permissions remain DSH-owned.
- Failure, timeout or absent device delegates to `next()`, so DSH's native approval UI remains the fallback.
- Session/status forwarding must use explicit read-only Remote event subscriptions and sanitized projections; it must not scrape the DOM.

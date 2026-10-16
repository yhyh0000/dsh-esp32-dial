/**
 * Agent Satellite Gateway control plane.
 *
 * It deliberately has no DSH/Codex credential and cannot run shell commands.
 * Local Bridges register Hosts and Sessions; Satellites discover routes through
 * this API. Production MQTT routing can consume the same Registry methods.
 */
import { createServer } from "node:http";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { randomUUID, timingSafeEqual } from "node:crypto";
import { fileURLToPath } from "node:url";

const PORT = Number(process.env.GATEWAY_PORT ?? 8787);
const HOST = process.env.GATEWAY_LISTEN_HOST ?? "127.0.0.1";
const DATA_DIR = process.env.GATEWAY_DATA_DIR ?? join(process.cwd(), "state");
const ADMIN_TOKEN = process.env.GATEWAY_ADMIN_TOKEN ?? "";
const FILE = join(DATA_DIR, "registry.json");
mkdirSync(DATA_DIR, { recursive: true });

function now() { return new Date().toISOString(); }
function safeId(value, max = 80) { return typeof value === "string" && new RegExp(`^[A-Za-z0-9._-]{1,${max}}$`).test(value); }
function safeText(value, max = 120) { return typeof value === "string" ? value.slice(0, max) : ""; }
function load() {
  try { return JSON.parse(readFileSync(FILE, "utf8")); }
  catch { return { version: 1, devices: {}, hosts: {}, routes: {}, audit: [] }; }
}
const db = load();
function save() { writeFileSync(FILE, `${JSON.stringify(db, null, 2)}\n`, { mode: 0o600 }); }
function audit(event, detail = {}) {
  db.audit.unshift({ id: randomUUID(), at: now(), event, ...detail });
  db.audit.splice(200, Infinity);
  save();
}
function json(res, code, body) { res.writeHead(code, { "content-type": "application/json; charset=utf-8" }); res.end(JSON.stringify(body)); }
function read(req) { return new Promise((resolve, reject) => { const c=[]; req.on("data", x=>c.push(x)); req.on("end",()=>{ try { resolve(c.length ? JSON.parse(Buffer.concat(c).toString("utf8")) : {}); } catch { reject(new Error("invalid JSON")); } }); req.on("error", reject); }); }
function authorized(req) {
  if (!ADMIN_TOKEN) return true;
  const supplied = Buffer.from(String(req.headers.authorization ?? "").replace(/^Bearer\s+/i, ""));
  const expected = Buffer.from(ADMIN_TOKEN);
  return supplied.length === expected.length && timingSafeEqual(supplied, expected);
}
function devicePublic(device) {
  const { credential, ...view } = device;
  return view;
}
function hostPublic(host) {
  return { ...host, sessions: Object.values(host.sessions ?? {}).map(({ id, title, agentType, state, canAcceptInput, canApprove }) => ({ id, title, agentType, state, canAcceptInput, canApprove })) };
}
function expire() {
  const deadline = Date.now() - 45_000;
  for (const device of Object.values(db.devices)) if (Date.parse(device.lastSeenAt ?? 0) < deadline) device.online = false;
  for (const host of Object.values(db.hosts)) if (Date.parse(host.lastSeenAt ?? 0) < deadline) host.online = false;
}
function routeView(route) {
  const device = db.devices[route.deviceId];
  const host = db.hosts[route.hostId];
  const session = host?.sessions?.[route.sessionId];
  return { ...route, device: device ? devicePublic(device) : null, host: host ? hostPublic(host) : null, session: session ? { ...session } : null };
}
function validRoute(body) {
  if (!safeId(body.deviceId) || !safeId(body.hostId) || !safeId(body.sessionId)) return "invalid id";
  const device = db.devices[body.deviceId], host = db.hosts[body.hostId], session = host?.sessions?.[body.sessionId];
  if (!device?.online) return "device offline or unknown";
  if (!host?.online) return "host offline or unknown";
  if (!session) return "session unknown";
  if (session.agentType !== body.agentType) return "agent/session mismatch";
  return null;
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url ?? "/", "http://gateway");
  const path = url.pathname;
  try {
    expire();
    if (req.method === "GET" && path === "/health") return json(res, 200, { ok: true, at: now(), devices: Object.keys(db.devices).length, hosts: Object.keys(db.hosts).length });
    if (!authorized(req)) return json(res, 401, { ok: false, error: "unauthorized" });

    if (req.method === "POST" && path === "/v1/devices/register") {
      const b = await read(req);
      if (!safeId(b.id) || !safeId(b.tenantId)) return json(res, 400, { ok: false, error: "invalid device id or tenant" });
      const old = db.devices[b.id] ?? {};
      db.devices[b.id] = { id: b.id, tenantId: b.tenantId, displayName: safeText(b.displayName, 48) || b.id, kind: safeText(b.kind, 64) || "esp32-satellite", capabilities: typeof b.capabilities === "object" && b.capabilities ? b.capabilities : {}, credential: old.credential ?? safeText(b.credential, 128), online: true, firmware: safeText(b.firmware, 48), lastSeenAt: now(), createdAt: old.createdAt ?? now() };
      audit("device_registered", { deviceId: b.id }); return json(res, 201, { ok: true, device: devicePublic(db.devices[b.id]) });
    }
    if (req.method === "POST" && path === "/v1/devices/presence") {
      const b = await read(req); const device = db.devices[b.deviceId];
      if (!device) return json(res, 404, { ok: false, error: "device not registered" });
      device.online = b.online !== false; device.lastSeenAt = now(); if (b.firmware) device.firmware = safeText(b.firmware, 48); audit("device_presence", { deviceId: device.id, online: device.online }); return json(res, 200, { ok: true });
    }
    if (req.method === "GET" && path === "/v1/devices") return json(res, 200, { ok: true, devices: Object.values(db.devices).map(devicePublic) });

    if (req.method === "POST" && path === "/v1/hosts/register") {
      const b = await read(req);
      if (!safeId(b.id) || !safeId(b.tenantId)) return json(res, 400, { ok: false, error: "invalid host id or tenant" });
      const old = db.hosts[b.id] ?? {};
      db.hosts[b.id] = { id: b.id, tenantId: b.tenantId, displayName: safeText(b.displayName, 48) || b.id, capabilities: typeof b.capabilities === "object" && b.capabilities ? b.capabilities : {}, online: true, lastSeenAt: now(), createdAt: old.createdAt ?? now(), sessions: old.sessions ?? {} };
      audit("host_registered", { hostId: b.id }); return json(res, 201, { ok: true, host: hostPublic(db.hosts[b.id]) });
    }
    if (req.method === "POST" && path === "/v1/hosts/presence") {
      const b = await read(req); const host = db.hosts[b.hostId]; if (!host) return json(res, 404, { ok: false, error: "host not registered" }); host.online = b.online !== false; host.lastSeenAt = now(); audit("host_presence", { hostId: host.id, online: host.online }); return json(res, 200, { ok: true });
    }
    if (req.method === "POST" && path === "/v1/sessions/upsert") {
      const b = await read(req); const host = db.hosts[b.hostId];
      if (!host || !safeId(b.session.id) || !["dsh", "codex", "vscode"].includes(b.session.agentType)) return json(res, 400, { ok: false, error: "invalid host or session" });
      host.sessions[b.session.id] = { id: b.session.id, agentType: b.session.agentType, title: safeText(b.session.title, 80), state: safeText(b.session.state, 20) || "idle", canAcceptInput: b.session.canAcceptInput === true, canApprove: b.session.canApprove === true, updatedAt: now() };
      host.lastSeenAt = now(); host.online = true; audit("session_upserted", { hostId: host.id, sessionId: b.session.id }); return json(res, 200, { ok: true, session: host.sessions[b.session.id] });
    }
    if (req.method === "GET" && path === "/v1/hosts") return json(res, 200, { ok: true, hosts: Object.values(db.hosts).map(hostPublic) });
    const hostMatch = path.match(/^\/v1\/hosts\/([A-Za-z0-9._-]+)\/sessions$/);
    if (req.method === "GET" && hostMatch) { const host = db.hosts[hostMatch[1]]; return host ? json(res, 200, { ok: true, sessions: Object.values(host.sessions ?? {}) }) : json(res, 404, { ok: false, error: "host not found" }); }

    if (req.method === "POST" && path === "/v1/routes/activate") {
      const b = await read(req); const invalid = validRoute(b); if (invalid) return json(res, 409, { ok: false, error: invalid });
      const route = { id: randomUUID(), deviceId: b.deviceId, hostId: b.hostId, agentType: b.agentType, sessionId: b.sessionId, role: ["primary", "observer", "alert", "kiosk"].includes(b.role) ? b.role : "primary", mode: ["once", "until_reboot", "persistent"].includes(b.mode) ? b.mode : "until_reboot", active: true, createdAt: now(), updatedAt: now() };
      db.routes[b.deviceId] = route; audit("route_activated", { deviceId: route.deviceId, hostId: route.hostId, sessionId: route.sessionId, role: route.role }); return json(res, 201, { ok: true, route: routeView(route) });
    }
    const routeMatch = path.match(/^\/v1\/routes\/([A-Za-z0-9._-]+)$/);
    if (req.method === "GET" && routeMatch) { const route = db.routes[routeMatch[1]]; return route ? json(res, 200, { ok: true, route: routeView(route) }) : json(res, 404, { ok: false, error: "route not found" }); }
    if (req.method === "GET" && path === "/v1/audit") return json(res, 200, { ok: true, events: db.audit.slice(0, 100) });
    return json(res, 404, { ok: false, error: "not found" });
  } catch (error) { return json(res, 400, { ok: false, error: error.message }); }
});
if (process.argv[1] === fileURLToPath(import.meta.url)) {
  server.listen(PORT, HOST, () => console.log(`Agent Satellite Gateway listening on http://${HOST}:${PORT}`));
}
export { db, validRoute, devicePublic, hostPublic, server };

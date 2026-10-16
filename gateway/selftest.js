import { strict as assert } from "node:assert";
import { db, validRoute, devicePublic } from "./gateway.js";

Object.assign(db, { version: 1, devices: {}, hosts: {}, routes: {}, audit: [] });
db.devices["badge-185b-office"] = { id: "badge-185b-office", tenantId: "personal", credential: "secret", online: true, capabilities: { display: true, touchApproval: true } };
db.hosts["host-windows-main"] = { id: "host-windows-main", tenantId: "personal", online: true, sessions: {
  "session-build-fix": { id: "session-build-fix", agentType: "dsh", state: "working", canAcceptInput: true, canApprove: true },
} };
assert.equal(validRoute({ deviceId: "badge-185b-office", hostId: "host-windows-main", agentType: "dsh", sessionId: "session-build-fix" }), null);
assert.equal(validRoute({ deviceId: "badge-185b-office", hostId: "host-windows-main", agentType: "codex", sessionId: "session-build-fix" }), "agent/session mismatch");
assert.equal(validRoute({ deviceId: "missing", hostId: "host-windows-main", agentType: "dsh", sessionId: "session-build-fix" }), "device offline or unknown");
assert.equal(devicePublic(db.devices["badge-185b-office"]).credential, undefined);
console.log("PASS gateway registry and route validation");

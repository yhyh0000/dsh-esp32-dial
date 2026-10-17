/*
 * Prototype browser Client Plugin entry.
 *
 * This source must be built against the exact DSH 0.2.0-rc.2 SDK; it is kept as
 * source because the installed runtime does not ship a public build scaffold.
 * Do NOT copy its authenticated browser state to Node, ESP32, or the Gateway.
 */

const BRIDGE = "http://127.0.0.1:3083";

async function post(path: string, body: unknown): Promise<Response> {
  return fetch(`${BRIDGE}${path}`, {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
  });
}

export const inject = ["remote", "sessions"];

export function apply(ctx: any): void {
  ctx.remote.$on("approval/request", async function (request: any, next: () => Promise<unknown>) {
    // Approval events are scoped. If DSH cannot resolve an active session, its
    // own UI remains the authority.
    const sessionId = ctx.sessions.scopeOf(this);
    if (sessionId === undefined) return next();

    try {
      const created = await post("/plugin/approval/request", {
        sessionId,
        toolName: String(request.toolName ?? "工具").slice(0, 28),
        reason: String(request.reason ?? "需要一次性确认").slice(0, 160),
        ttlMs: 60_000,
      });
      if (!created.ok) return next();
      const { approvalId } = await created.json();
      if (typeof approvalId !== "string") return next();

      const outcome = await fetch(`${BRIDGE}/plugin/approval/${encodeURIComponent(approvalId)}/wait`);
      if (outcome.status !== 200) return next();
      const decision = await outcome.json();
      return decision?.outcome === "allowed-once" ? "allowed-once" : "rejected";
    } catch {
      // Never block DSH just because the optional physical satellite is absent.
      return next();
    }
  });
}

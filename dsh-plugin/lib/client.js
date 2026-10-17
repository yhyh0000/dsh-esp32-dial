/*
 * Prebuilt DSH web-plugin bundle for runtime 0.2.0-rc.2.
 *
 * The DSH module loader calls this factory in the authenticated browser client.
 * No cookie, launch token, or Host RPC credential crosses the localhost relay.
 */
window.__ModuleLoader__.load({
  id: "@suhmyh/dsh-esp32-satellite",
  factory: () => {
    var module = { exports: {} };
    var exports = module.exports;
    const BRIDGE = "http://127.0.0.1:3083";
    const inject = ["remote", "sessions"];
    async function relay(request, sessionId) {
      const created = await fetch(`${BRIDGE}/plugin/approval/request`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
          sessionId,
          toolName: String(request.toolName || "工具").slice(0, 28),
          reason: String(request.reason || "需要一次性确认").slice(0, 160),
          ttlMs: 60000,
        }),
      });
      if (!created.ok) return null;
      const value = await created.json();
      if (!value || typeof value.approvalId !== "string") return null;
      const answer = await fetch(`${BRIDGE}/plugin/approval/${encodeURIComponent(value.approvalId)}/wait`);
      if (answer.status !== 200) return null;
      const decision = await answer.json();
      return decision && (decision.outcome === "allowed-once" || decision.outcome === "rejected") ? decision.outcome : null;
    }
    function apply(ctx) {
      ctx.remote.$on("approval/request", async function(request, next) {
        const sessionId = ctx.sessions.scopeOf(this);
        if (sessionId === undefined) return next();
        try {
          const outcome = await relay(request, sessionId);
          return outcome === null ? next() : outcome;
        } catch (_) {
          return next();
        }
      });
    }
    exports.inject = inject;
    exports.apply = apply;
    return module.exports;
  },
});

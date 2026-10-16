/**
 * Optional client for the Agent Satellite Gateway registry.
 *
 * This reports Host/Session metadata only. It does not receive DSH credentials,
 * execute commands, or alter local DSH approvals. MQTT data-plane routing will
 * use the route records created by this control-plane registration.
 */
export class GatewayClient {
  constructor({ baseUrl = "", token = "", tenantId = "personal", hostId = "", displayName = "" } = {}) {
    this.baseUrl = String(baseUrl).replace(/\/$/, "");
    this.token = token;
    this.tenantId = tenantId;
    this.hostId = hostId;
    this.displayName = displayName || hostId;
    this.available = false;
  }

  get enabled() { return Boolean(this.baseUrl && this.hostId); }

  async request(path, body) {
    if (!this.enabled) return null;
    const response = await fetch(`${this.baseUrl}${path}`, {
      method: "POST",
      headers: {
        "content-type": "application/json",
        ...(this.token ? { authorization: `Bearer ${this.token}` } : {}),
      },
      body: JSON.stringify(body),
      signal: AbortSignal.timeout(5000),
    });
    if (!response.ok) throw new Error(`Gateway ${path}: HTTP ${response.status}`);
    return response.json();
  }

  async register(capabilities) {
    return this.request("/v1/hosts/register", {
      id: this.hostId,
      tenantId: this.tenantId,
      displayName: this.displayName,
      capabilities,
    });
  }

  async presence(online = true) {
    return this.request("/v1/hosts/presence", { hostId: this.hostId, online });
  }

  async upsertSession(session) {
    return this.request("/v1/sessions/upsert", { hostId: this.hostId, session });
  }
}

/* IMAP adapter kept on the bridge host; credentials never cross back to the dial. */

const PROVIDERS = {
	qq: { host: "imap.qq.com", port: 993 },
	gmail: { host: "imap.gmail.com", port: 993 },
};

function configured(account) {
	return Boolean(account?.email && account?.appPassword);
}

function safeDate(value) {
	const time = value instanceof Date ? value.getTime() : Date.parse(value ?? "");
	return Number.isFinite(time) ? time : 0;
}

/** Fetch a small, read-only inbox snapshot. ImapFlow is loaded by the bridge package. */
async function fetchMailAccount(provider, account) {
	if (!configured(account)) {
		return { provider, status: "unconfigured", unread: 0, items: [], error: "missing credentials" };
	}
	const endpoint = PROVIDERS[provider];
	if (!endpoint) return { provider, status: "offline", unread: 0, items: [], error: "unsupported provider" };

	const { ImapFlow } = await import("imapflow");
	const client = new ImapFlow({
		host: endpoint.host,
		port: endpoint.port,
		secure: true,
		auth: { user: account.email, pass: account.appPassword },
		logger: false,
	});
	try {
		await client.connect();
		const lock = await client.getMailboxLock("INBOX");
		try {
			const exists = Number(client.mailbox?.exists ?? 0);
			const start = Math.max(1, exists - 9);
			const items = [];
			for await (const message of client.fetch(`${start}:*`, {
				envelope: true,
				flags: true,
				internalDate: true,
			})) {
				const envelope = message.envelope ?? {};
				const sender = envelope.from?.[0]?.address ?? envelope.from?.[0]?.name ?? "";
				items.push({
					provider,
					subject: envelope.subject || "(无主题)",
					sender,
					time: (message.internalDate instanceof Date ? message.internalDate : new Date()).toISOString(),
					read: message.flags?.has("\\Seen") === true,
				});
			}
			const unread = (await client.search({ seen: false }))?.length ?? 0;
			items.sort((a, b) => safeDate(b.time) - safeDate(a.time));
			return { provider, status: "online", unread, items, error: "" };
		} finally {
			lock.release();
		}
	} catch (error) {
		return { provider, status: "offline", unread: 0, items: [], error: error?.message ?? "IMAP request failed" };
	} finally {
		try { await client.logout(); } catch { /* connection may already be closed */ }
	}
}

export { fetchMailAccount };

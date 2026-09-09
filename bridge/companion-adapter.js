/* Normalize external service responses before they cross the device boundary. */

function object(value) {
	return value !== null && typeof value === "object" && !Array.isArray(value) ? value : {};
}

function number(...values) {
	for (const value of values) {
		if (typeof value === "number" && Number.isFinite(value)) return value;
	}
	return undefined;
}

function integer(...values) {
	const value = number(...values);
	return value === undefined ? undefined : Math.round(value);
}

function percent(value) {
	if (value === undefined) return undefined;
	return Math.max(0, Math.min(100, Math.round(value)));
}

function epochMs(value) {
	if (typeof value === "number" && Number.isFinite(value)) return value < 1e12 ? Math.round(value * 1000) : Math.round(value);
	if (typeof value === "string" && value) {
		const parsed = Date.parse(value);
		return Number.isFinite(parsed) ? parsed : undefined;
	}
	return undefined;
}

function normalizeWindow(legacy, current) {
	const oldWindow = object(legacy);
	const newWindow = object(current);
	const usedPercent = percent(number(newWindow.used_percent, newWindow.usedPercent, oldWindow.utilization));
	const remainingPercent = percent(number(
		newWindow.remaining_percent,
		newWindow.remainingPercent,
		oldWindow.remaining_percent,
		oldWindow.remainingPercent,
		usedPercent === undefined ? undefined : 100 - usedPercent,
	));
	return {
		usedPercent: usedPercent ?? null,
		remainingPercent: remainingPercent ?? null,
		resetAt: epochMs(newWindow.reset_at ?? newWindow.resetAt ?? oldWindow.resets_at),
		remainingSeconds: integer(newWindow.reset_after_seconds, newWindow.resetAfterSeconds, oldWindow.remaining_seconds, oldWindow.remainingSeconds),
	};
}

/** Convert either known Sub2API quota response into the device contract. */
function normalizeQuotaResponse(payload, now = Date.now()) {
	const root = object(payload);
	const data = object(root.data ?? root);
	const rateLimit = object(data.rate_limit ?? data.rateLimit);
	const primary = object(rateLimit.primary_window ?? rateLimit.primaryWindow);
	const secondary = object(rateLimit.secondary_window ?? rateLimit.secondaryWindow);
	const fiveHour = normalizeWindow(data.five_hour ?? data.fiveHour, primary);
	const sevenDay = normalizeWindow(data.seven_day ?? data.sevenDay, secondary);
	const resetCredits = object(rateLimit.rate_limit_reset_credits ?? data.rate_limit_reset_credits ?? data.rateLimitResetCredits);
	const credits = Array.isArray(resetCredits.credits) ? resetCredits.credits : [];
	const availableCount = integer(resetCredits.available_count, resetCredits.availableCount, credits.length) ?? 0;
	const futureCredits = credits
		.map((credit) => epochMs(object(credit).expires_at ?? object(credit).expiresAt))
		.filter((value) => value !== undefined && value > now)
		.sort((a, b) => a - b);
	const limitReached = rateLimit.limit_reached === true || rateLimit.limitReached === true;
	const allowed = rateLimit.allowed;
	const used = [fiveHour.usedPercent, sevenDay.usedPercent].filter((value) => value !== null);
	let status = "unknown";
	if (limitReached || allowed === false || used.some((value) => value >= 100)) status = "exhausted";
	else if (used.length > 0) status = used.some((value) => value >= 80) ? "low" : "available";

	return {
		provider: "sub2api",
		plan: typeof data.plan_type === "string" ? data.plan_type : (typeof data.planType === "string" ? data.planType : "unknown"),
		windows: { fiveHour, sevenDay },
		resetCards: { availableCount, nextExpiresAt: futureCredits[0] ?? null },
		status,
		updatedAt: now,
	};
}

export { normalizeQuotaResponse };

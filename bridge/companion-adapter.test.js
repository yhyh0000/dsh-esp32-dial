import assert from "node:assert/strict";
import test from "node:test";
import { normalizeQuotaResponse } from "./companion-adapter.js";

test("normalizes the legacy quota response", () => {
	const result = normalizeQuotaResponse({
		code: 0,
		data: {
			updated_at: "2026-09-08T21:31:59+08:00",
			five_hour: { utilization: 48, resets_at: "2026-09-08T23:05:45+08:00", remaining_seconds: 5624 },
			seven_day: { utilization: 13, resets_at: "2026-09-15T13:05:45+08:00", remaining_seconds: 574424 },
		},
	}, 1788874337000);
	assert.equal(result.status, "available");
	assert.equal(result.windows.fiveHour.usedPercent, 48);
	assert.equal(result.windows.fiveHour.remainingPercent, 52);
	assert.equal(result.windows.sevenDay.usedPercent, 13);
});

test("normalizes the rate_limit response and reset credits", () => {
	const result = normalizeQuotaResponse({
		code: 0,
		data: {
			plan_type: "plus",
			rate_limit: {
				allowed: true,
				primary_window: { used_percent: 48, reset_after_seconds: 5608, reset_at: 1788879945 },
				secondary_window: { used_percent: 13, reset_after_seconds: 574408, reset_at: 1789448744 },
				rate_limit_reset_credits: { available_count: 3, credits: [{ expires_at: "2026-09-21T09:52:00.026Z" }] },
			},
		},
	}, 1788874337000);
	assert.equal(result.plan, "plus");
	assert.equal(result.windows.fiveHour.remainingPercent, 52);
	assert.equal(result.windows.fiveHour.remainingSeconds, 5608);
	assert.equal(result.resetCards.availableCount, 3);
	assert.equal(result.status, "available");
});

test("does not call missing fields normal", () => {
	const result = normalizeQuotaResponse({ code: 502, message: "upstream unavailable" }, 1788874337000);
	assert.equal(result.status, "unknown");
	assert.equal(result.windows.fiveHour.remainingPercent, null);
	assert.equal(result.resetCards.availableCount, 0);
});

import assert from "node:assert/strict";
import {webcrypto} from "node:crypto";
import {
  MAX_NONCE,
  chooseUnusedNonce,
  dayIndexFor,
  encodeToken,
  runSelfTest,
  tierForMinutes,
  todayUtc8,
} from "../../tools/ptc_frontend/token.js";
import {
  CONFIG_KEY,
  DEMO_ACK_KEY,
  DEFAULT_CONFIG,
  NONCES_KEY,
  clearFrontendState,
  loadConfig,
  rememberNonce,
  saveConfig,
  usedNoncesFor,
} from "../../tools/ptc_frontend/storage.js";
import {
  DEMO_SECRET,
  isDemoSecret,
  maskedSecret,
  pairingFromFragment,
  pairingFromImportText,
  validatePairing,
} from "../../tools/ptc_frontend/pairing.js";

class MemoryStorage {
  constructor() { this.values = new Map(); }
  getItem(key) { return this.values.has(key) ? this.values.get(key) : null; }
  setItem(key, value) { this.values.set(key, String(value)); }
  removeItem(key) { this.values.delete(key); }
}

assert.equal(await runSelfTest(webcrypto), true);
assert.equal(dayIndexFor("2026-07-08"), 2380);
assert.equal(todayUtc8(Date.UTC(2026, 6, 7, 16, 0, 0)), "2026-07-08");
assert.throws(() => dayIndexFor("2026-02-30"), /日期无效/);

for (let minutes = 5; minutes <= 120; minutes += 5) {
  assert.equal(tierForMinutes(minutes), minutes / 5 - 1);
}
assert.equal(tierForMinutes(150), 28);
assert.equal(tierForMinutes(240), 31);
assert.throws(() => tierForMinutes(7), /150\/180\/210\/240/);

assert.equal(DEMO_SECRET.length, 32);
assert.equal(DEFAULT_CONFIG.secret, DEMO_SECRET);
assert.equal(isDemoSecret(DEMO_SECRET), true);
assert.equal(maskedSecret(DEMO_SECRET).startsWith("play"), true);
assert.deepEqual(
  pairingFromFragment(`#device_id=kid-switch&grant_secret=${DEMO_SECRET}`),
  {deviceId: "kid-switch", secret: DEMO_SECRET},
);
assert.throws(() => pairingFromFragment("#device_id=kid-switch"), /参数不完整/);
assert.deepEqual(
  pairingFromImportText(JSON.stringify({version: 1, device_id: "family_switch", grant_secret: "a".repeat(64)})),
  {deviceId: "family_switch", secret: "a".repeat(64)},
);
assert.throws(() => pairingFromImportText("{"), /有效的 JSON/);
assert.throws(() => validatePairing({deviceId: "bad space", secret: "a".repeat(32)}), /设备 ID/);
assert.throws(() => validatePairing({deviceId: "ok", secret: "short"}), /32–64/);

assert.equal(await encodeToken({
  deviceId: "test-device",
  secret: "test-secret",
  dayIndex: 2380,
  tierIndex: 0,
  nonce: 0,
}, webcrypto), "00002848");
assert.rejects(() => encodeToken({
  deviceId: "test-device",
  secret: "test-secret",
  dayIndex: 2380,
  tierIndex: 0,
  nonce: MAX_NONCE + 1,
}, webcrypto), /0\.\.511/);
assert.rejects(() => encodeToken({
  deviceId: "中".repeat(27),
  secret: "test-secret",
  dayIndex: 2380,
  tierIndex: 0,
  nonce: 0,
}, webcrypto), /79 字节/);

const deterministicCrypto = {
  subtle: webcrypto.subtle,
  values: [5, 5, 9],
  getRandomValues(array) {
    array[0] = this.values.shift();
    return array;
  },
};
assert.equal(chooseUnusedNonce(new Set([5]), deterministicCrypto), 9);
assert.throws(() => chooseUnusedNonce(new Set(Array.from({length: 512}, (_, index) => index)), webcrypto), /512/);

const storage = new MemoryStorage();
assert.deepEqual(loadConfig(storage), DEFAULT_CONFIG);
saveConfig(storage, {deviceId: "family-switch", secret: "family-secret", tierMinutes: 45});
assert.deepEqual(loadConfig(storage), {deviceId: "family-switch", secret: "family-secret", tierMinutes: 45});
assert.ok(storage.getItem(CONFIG_KEY).includes("family-secret"));

rememberNonce(storage, "family-switch", "2026-07-08", 7);
rememberNonce(storage, "family-switch", "2026-07-08", 7);
rememberNonce(storage, "family-switch", "2026-07-08", 11);
assert.deepEqual([...usedNoncesFor(storage, "family-switch", "2026-07-08")], [7, 11]);
assert.ok(storage.getItem(NONCES_KEY));

clearFrontendState(storage);
assert.equal(storage.getItem(CONFIG_KEY), null);
assert.equal(storage.getItem(NONCES_KEY), null);
assert.equal(storage.getItem(DEMO_ACK_KEY), null);
assert.deepEqual(loadConfig(storage), DEFAULT_CONFIG);

console.log("PTC frontend JavaScript tests passed");

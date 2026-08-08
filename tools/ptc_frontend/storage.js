import {DEMO_SECRET} from "./pairing.js";

export const CONFIG_KEY = "ptc.frontend.config.v1";
export const NONCES_KEY = "ptc.frontend.nonces.v1";
export const DEMO_ACK_KEY = "ptc.frontend.demo-ack.v1";

export const DEFAULT_CONFIG = Object.freeze({
  deviceId: "kid-switch",
  secret: DEMO_SECRET,
  tierMinutes: 30,
});

function parseObject(text) {
  if (!text) return {};
  try {
    const value = JSON.parse(text);
    return value && typeof value === "object" && !Array.isArray(value) ? value : {};
  } catch {
    return {};
  }
}

export function loadConfig(storage) {
  const saved = parseObject(storage.getItem(CONFIG_KEY));
  const tierMinutes = Number(saved.tierMinutes);
  return {
    deviceId: typeof saved.deviceId === "string" && saved.deviceId ? saved.deviceId : DEFAULT_CONFIG.deviceId,
    secret: typeof saved.secret === "string" && saved.secret ? saved.secret : DEFAULT_CONFIG.secret,
    tierMinutes: Number.isInteger(tierMinutes) && tierMinutes >= 5 && tierMinutes <= 120 && tierMinutes % 5 === 0
      ? tierMinutes
      : DEFAULT_CONFIG.tierMinutes,
  };
}

export function saveConfig(storage, config) {
  storage.setItem(CONFIG_KEY, JSON.stringify({
    deviceId: config.deviceId,
    secret: config.secret,
    tierMinutes: Number(config.tierMinutes),
  }));
}

export function nonceStorageKey(deviceId, dateText) {
  return `${deviceId}\0${dateText}`;
}

export function loadNonceState(storage) {
  return parseObject(storage.getItem(NONCES_KEY));
}

export function usedNoncesFor(storage, deviceId, dateText) {
  const state = loadNonceState(storage);
  const values = state[nonceStorageKey(deviceId, dateText)];
  if (!Array.isArray(values)) return new Set();
  return new Set(values.map(Number).filter(value => Number.isInteger(value) && value >= 0 && value <= 511));
}

export function rememberNonce(storage, deviceId, dateText, nonce) {
  const state = loadNonceState(storage);
  const key = nonceStorageKey(deviceId, dateText);
  const values = usedNoncesFor(storage, deviceId, dateText);
  values.add(nonce);
  state[key] = [...values].sort((left, right) => left - right);
  storage.setItem(NONCES_KEY, JSON.stringify(state));
}

export function clearFrontendState(storage) {
  storage.removeItem(CONFIG_KEY);
  storage.removeItem(NONCES_KEY);
  storage.removeItem(DEMO_ACK_KEY);
}

export function demoRiskAcknowledged(storage) {
  return storage.getItem(DEMO_ACK_KEY) === "yes";
}

export function acknowledgeDemoRisk(storage) {
  storage.setItem(DEMO_ACK_KEY, "yes");
}

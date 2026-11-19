import {DEMO_SECRET} from "./pairing.js";

export const CONFIG_KEY = "ptc.frontend.config.v1";
export const NONCES_KEY = "ptc.frontend.nonces.v1";
export const DEMO_ACK_KEY = "ptc.frontend.demo-ack.v1";
export const LANG_KEY = "ptc.frontend.lang.v1";
const DEVICES_KEY = "ptc.frontend.devices.v1";
const ISSUED_PREFIX = "issued\0";

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

function normalizedConfig(saved) {
  const tierMinutes = Number(saved.tierMinutes);
  return {
    deviceId: typeof saved.deviceId === "string" && saved.deviceId ? saved.deviceId : DEFAULT_CONFIG.deviceId,
    secret: typeof saved.secret === "string" && saved.secret ? saved.secret : DEFAULT_CONFIG.secret,
    tierMinutes: Number.isInteger(tierMinutes) &&
        ((tierMinutes >= 1 && tierMinutes < 5) ||
         (tierMinutes >= 5 && tierMinutes <= 120 && tierMinutes % 5 === 0) ||
         [150, 180, 210, 240].includes(tierMinutes))
      ? tierMinutes
      : DEFAULT_CONFIG.tierMinutes,
  };
}

export function loadConfig(storage) {
  return normalizedConfig(parseObject(storage.getItem(CONFIG_KEY)));
}

export function loadDevices(storage) {
  const devices = new Map();
  for (const saved of Object.values(parseObject(storage.getItem(DEVICES_KEY)))) {
    if (saved?.deviceId && saved.secret) devices.set(saved.deviceId, normalizedConfig(saved));
  }
  const active = parseObject(storage.getItem(CONFIG_KEY));
  if (active.deviceId && active.secret) devices.set(active.deviceId, normalizedConfig(active));
  return devices;
}

export function saveConfig(storage, config) {
  const active = {
    deviceId: config.deviceId,
    secret: config.secret,
    tierMinutes: Number(config.tierMinutes),
  };
  const devices = loadDevices(storage);
  devices.set(active.deviceId, active);
  storage.setItem(DEVICES_KEY, JSON.stringify(Object.fromEntries(devices)));
  storage.setItem(CONFIG_KEY, JSON.stringify(active));
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

export function issuedFor(storage, deviceId, dateText) {
  const records = loadNonceState(storage)[ISSUED_PREFIX + nonceStorageKey(deviceId, dateText)];
  return Array.isArray(records) ? records.filter(record => record &&
    Number.isFinite(record.time) && Math.abs(record.time) <= 8.64e15 &&
    Number.isInteger(record.minutes) && typeof record.suffix === "string" && /^\d{4}$/.test(record.suffix)) : [];
}

export function rememberNonce(storage, deviceId, dateText, nonce, receipt) {
  const state = loadNonceState(storage);
  const key = nonceStorageKey(deviceId, dateText);
  const values = usedNoncesFor(storage, deviceId, dateText);
  if (values.has(nonce)) return false;
  values.add(nonce);
  state[key] = [...values].sort((left, right) => left - right);
  if (receipt) state[ISSUED_PREFIX + key] = [receipt, ...issuedFor(storage, deviceId, dateText)];
  storage.setItem(NONCES_KEY, JSON.stringify(state));
  return true;
}

export function clearFrontendState(storage) {
  storage.removeItem(CONFIG_KEY);
  storage.removeItem(DEVICES_KEY);
  storage.removeItem(NONCES_KEY);
  storage.removeItem(DEMO_ACK_KEY);
}

export function demoRiskAcknowledged(storage) {
  return storage.getItem(DEMO_ACK_KEY) === "yes";
}

export function acknowledgeDemoRisk(storage) {
  storage.setItem(DEMO_ACK_KEY, "yes");
}

export function loadLanguage(storage) {
  const saved = storage.getItem(LANG_KEY);
  if (saved === "zh" || saved === "en") return saved;
  return null;
}

export function saveLanguage(storage, lang) {
  if (lang === "zh" || lang === "en") {
    storage.setItem(LANG_KEY, lang);
  }
}


export const TOKEN_DOMAIN = "PTC2";
export const TOKEN_LENGTH = 8;
export const TIER_MINUTES = 5;
export const TIER_COUNT = 28;
export const MAX_NONCE = 511;
export const MAC_BITS = 12;
export const MAX_DAY_INDEX = 0xffff;

const DAY_MS = 24 * 60 * 60 * 1000;
const EPOCH_UTC_MS = Date.UTC(2020, 0, 1);

export function todayUtc8(nowMs = Date.now()) {
  return new Date(nowMs + 8 * 60 * 60 * 1000).toISOString().slice(0, 10);
}

export function dayIndexFor(dateText) {
  if (!/^\d{4}-\d{2}-\d{2}$/.test(dateText)) throw new Error("日期格式必须为 YYYY-MM-DD");
  const [year, month, day] = dateText.split("-").map(Number);
  const utcMs = Date.UTC(year, month - 1, day);
  const normalized = new Date(utcMs).toISOString().slice(0, 10);
  if (normalized !== dateText) throw new Error("日期无效");
  const dayIndex = (utcMs - EPOCH_UTC_MS) / DAY_MS;
  if (!Number.isInteger(dayIndex) || dayIndex < 0 || dayIndex > MAX_DAY_INDEX) throw new Error("日期超出协议范围");
  return dayIndex;
}

export function tierForMinutes(minutes) {
  const value = Number(minutes);
  if (!Number.isInteger(value)) throw new Error("加时时长必须为整数");
  if (value >= 1 && value <= 4) return 23 + value;
  if (value >= 5 && value <= 120 && value % TIER_MINUTES === 0) {
    return value / TIER_MINUTES - 1;
  }
  throw new Error("加时时长必须是 1-4 分钟或 5 至 120 分钟之间的 5 分钟档位");
}

function requireWebCrypto(cryptoApi) {
  if (!cryptoApi?.subtle || typeof cryptoApi.getRandomValues !== "function") {
    throw new Error("当前页面无法使用 Web Crypto，请通过 HTTPS 或 localhost 打开");
  }
}

export async function encodeToken({deviceId, secret, dayIndex, tierIndex, nonce}, cryptoApi = globalThis.crypto) {
  requireWebCrypto(cryptoApi);
  if (!deviceId) throw new Error("设备 ID 不能为空");
  if (!secret) throw new Error("加时密钥不能为空");
  if (!Number.isInteger(dayIndex) || dayIndex < 0 || dayIndex > MAX_DAY_INDEX) throw new Error("日期索引超出协议范围");
  if (!Number.isInteger(tierIndex) || tierIndex < 0 || tierIndex >= TIER_COUNT) throw new Error("加时时长档位无效");
  if (!Number.isInteger(nonce) || nonce < 0 || nonce > MAX_NONCE) throw new Error("随机编号超出 0..511");

  const encoder = new TextEncoder();
  const domain = encoder.encode(TOKEN_DOMAIN);
  const deviceBytes = encoder.encode(deviceId);
  if (deviceBytes.length > 79) throw new Error("设备 ID 的 UTF-8 长度不能超过 79 字节");
  const message = new Uint8Array(domain.length + deviceBytes.length + 1 + 2 + 1 + 2);
  let offset = 0;
  message.set(domain, offset); offset += domain.length;
  message.set(deviceBytes, offset); offset += deviceBytes.length;
  message[offset++] = 0;
  message[offset++] = dayIndex >> 8;
  message[offset++] = dayIndex & 0xff;
  message[offset++] = tierIndex;
  message[offset++] = nonce >> 8;
  message[offset] = nonce & 0xff;

  const key = await cryptoApi.subtle.importKey(
    "raw",
    encoder.encode(secret),
    {name: "HMAC", hash: "SHA-256"},
    false,
    ["sign"],
  );
  const digest = new Uint8Array(await cryptoApi.subtle.sign("HMAC", key, message));
  const mac = ((digest[0] << 8) | digest[1]) >> (16 - MAC_BITS);
  const value = tierIndex * 2 ** 21 + nonce * 2 ** MAC_BITS + mac;
  return String(value).padStart(TOKEN_LENGTH, "0");
}

export function chooseUnusedNonce(usedNonces, cryptoApi = globalThis.crypto) {
  requireWebCrypto(cryptoApi);
  const used = new Set([...usedNonces].map(Number));
  if (used.size >= MAX_NONCE + 1) throw new Error("当前浏览器今天已签发 512 个加时码，请明天再试");
  const random = new Uint16Array(1);
  for (let attempt = 0; attempt <= MAX_NONCE; attempt += 1) {
    cryptoApi.getRandomValues(random);
    const nonce = random[0] & MAX_NONCE;
    if (!used.has(nonce)) return nonce;
  }
  for (let nonce = 0; nonce <= MAX_NONCE; nonce += 1) {
    if (!used.has(nonce)) return nonce;
  }
  throw new Error("无法分配新的随机编号");
}

export async function runSelfTest(cryptoApi = globalThis.crypto) {
  const code = await encodeToken({
    deviceId: "test-device",
    secret: "test-secret",
    dayIndex: 2380,
    tierIndex: 5,
    nonce: 7,
  }, cryptoApi);
  if (code !== "10514680") throw new Error(`PTC2 自检结果不匹配：${code}`);
  return true;
}

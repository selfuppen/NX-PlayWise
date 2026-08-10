export const TOKEN_DOMAIN = "PTC2";
export const TOKEN_LENGTH = 8;
export const TIER_MINUTES = 5;
export const TIER_COUNT = 32;
export const MAX_NONCE = 511;
export const MAC_BITS = 12;
export const MAX_DAY_INDEX = 0xffff;

const DAY_MS = 24 * 60 * 60 * 1000;
const EPOCH_UTC_MS = Date.UTC(2020, 0, 1);
const SHA256_BLOCK_SIZE = 64;
const SHA256_INITIAL = new Uint32Array([
  0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
  0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
]);
const SHA256_ROUND = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
  0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
  0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
  0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
  0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
  0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
  0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);
let fallbackNonceCounter = 0;

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
  if (value === 150) return 28;
  if (value === 180) return 29;
  if (value === 210) return 30;
  if (value === 240) return 31;
  throw new Error("加时时长必须是 1-4 分钟、5 至 120 分钟（每 5 分钟一档）或 150/180/210/240 分钟");
}

function rotateRight(value, shift) {
  return (value >>> shift) | (value << (32 - shift));
}

function concatenateBytes(...parts) {
  const result = new Uint8Array(parts.reduce((total, part) => total + part.length, 0));
  let offset = 0;
  for (const part of parts) {
    result.set(part, offset);
    offset += part.length;
  }
  return result;
}

// The standalone file can run where SubtleCrypto is unavailable. This small
// implementation preserves the protocol's exact SHA-256/HMAC output.
function sha256(bytes) {
  const paddedLength = Math.ceil((bytes.length + 9) / SHA256_BLOCK_SIZE) * SHA256_BLOCK_SIZE;
  const padded = new Uint8Array(paddedLength);
  padded.set(bytes);
  padded[bytes.length] = 0x80;
  const bitLength = bytes.length * 8;
  const view = new DataView(padded.buffer);
  view.setUint32(paddedLength - 8, Math.floor(bitLength / 2 ** 32), false);
  view.setUint32(paddedLength - 4, bitLength >>> 0, false);

  const hash = new Uint32Array(SHA256_INITIAL);
  const words = new Uint32Array(64);
  for (let block = 0; block < paddedLength; block += SHA256_BLOCK_SIZE) {
    for (let index = 0; index < 16; index += 1) {
      words[index] = view.getUint32(block + index * 4, false);
    }
    for (let index = 16; index < 64; index += 1) {
      const left = words[index - 15];
      const right = words[index - 2];
      const sigma0 = rotateRight(left, 7) ^ rotateRight(left, 18) ^ (left >>> 3);
      const sigma1 = rotateRight(right, 17) ^ rotateRight(right, 19) ^ (right >>> 10);
      words[index] = (words[index - 16] + sigma0 + words[index - 7] + sigma1) >>> 0;
    }

    let [a, b, c, d, e, f, g, h] = hash;
    for (let index = 0; index < 64; index += 1) {
      const sum1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
      const choice = (e & f) ^ (~e & g);
      const temp1 = (h + sum1 + choice + SHA256_ROUND[index] + words[index]) >>> 0;
      const sum0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
      const majority = (a & b) ^ (a & c) ^ (b & c);
      const temp2 = (sum0 + majority) >>> 0;
      h = g;
      g = f;
      f = e;
      e = (d + temp1) >>> 0;
      d = c;
      c = b;
      b = a;
      a = (temp1 + temp2) >>> 0;
    }
    hash[0] = (hash[0] + a) >>> 0;
    hash[1] = (hash[1] + b) >>> 0;
    hash[2] = (hash[2] + c) >>> 0;
    hash[3] = (hash[3] + d) >>> 0;
    hash[4] = (hash[4] + e) >>> 0;
    hash[5] = (hash[5] + f) >>> 0;
    hash[6] = (hash[6] + g) >>> 0;
    hash[7] = (hash[7] + h) >>> 0;
  }

  const digest = new Uint8Array(32);
  const digestView = new DataView(digest.buffer);
  hash.forEach((value, index) => digestView.setUint32(index * 4, value, false));
  return digest;
}

function hmacSha256(key, message) {
  const normalizedKey = key.length > SHA256_BLOCK_SIZE ? sha256(key) : key;
  const innerPad = new Uint8Array(SHA256_BLOCK_SIZE);
  const outerPad = new Uint8Array(SHA256_BLOCK_SIZE);
  for (let index = 0; index < SHA256_BLOCK_SIZE; index += 1) {
    const value = normalizedKey[index] || 0;
    innerPad[index] = value ^ 0x36;
    outerPad[index] = value ^ 0x5c;
  }
  return sha256(concatenateBytes(outerPad, sha256(concatenateBytes(innerPad, message))));
}

export async function encodeToken({deviceId, secret, dayIndex, tierIndex, nonce}, cryptoApi = globalThis.crypto) {
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

  let digest;
  if (cryptoApi?.subtle) {
    const key = await cryptoApi.subtle.importKey(
      "raw",
      encoder.encode(secret),
      {name: "HMAC", hash: "SHA-256"},
      false,
      ["sign"],
    );
    digest = new Uint8Array(await cryptoApi.subtle.sign("HMAC", key, message));
  } else {
    digest = hmacSha256(encoder.encode(secret), message);
  }
  const mac = ((digest[0] << 8) | digest[1]) >> (16 - MAC_BITS);
  const value = tierIndex * 2 ** 21 + nonce * 2 ** MAC_BITS + mac;
  return String(value).padStart(TOKEN_LENGTH, "0");
}

export function chooseUnusedNonce(usedNonces, cryptoApi = globalThis.crypto) {
  const used = new Set([...usedNonces].map(Number));
  if (used.size >= MAX_NONCE + 1) throw new Error("当前浏览器今天已签发 512 个加时码，请明天再试");
  if (typeof cryptoApi?.getRandomValues === "function") {
    const random = new Uint16Array(1);
    for (let attempt = 0; attempt <= MAX_NONCE; attempt += 1) {
      cryptoApi.getRandomValues(random);
      const nonce = random[0] & MAX_NONCE;
      if (!used.has(nonce)) return nonce;
    }
  }

  const start = (Date.now() + fallbackNonceCounter) & MAX_NONCE;
  fallbackNonceCounter = (fallbackNonceCounter + 1) & MAX_NONCE;
  for (let offset = 0; offset <= MAX_NONCE; offset += 1) {
    const nonce = (start + offset) & MAX_NONCE;
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

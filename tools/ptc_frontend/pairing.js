export const DEMO_SECRET = "playwise-public-demo-secret-0001";

const DEVICE_ID_PATTERN = /^[A-Za-z0-9_-]{1,32}$/;
const SECRET_PATTERN = /^[\x21-\x7e]{32,64}$/;

export function validatePairing({deviceId, secret}) {
  if (!DEVICE_ID_PATTERN.test(deviceId || "")) {
    throw new Error("设备 ID 必须为 1–32 位，只能包含字母、数字、- 和 _");
  }
  if (!SECRET_PATTERN.test(secret || "")) {
    throw new Error("加时密钥必须为 32–64 个非空白可打印 ASCII 字符");
  }
  return {deviceId, secret};
}

export function pairingFromFragment(fragment) {
  const raw = String(fragment || "").replace(/^#/, "");
  if (!raw) return null;
  const params = new URLSearchParams(raw);
  const deviceId = params.get("device_id");
  const secret = params.get("grant_secret");
  if (deviceId === null && secret === null) return null;
  if (deviceId === null || secret === null) {
    throw new Error("二维码参数不完整，需要 device_id 和 grant_secret");
  }
  return validatePairing({deviceId, secret});
}

export function pairingFromImportText(text) {
  let value;
  try {
    value = JSON.parse(text);
  } catch {
    throw new Error("配置文件不是有效的 JSON");
  }
  if (!value || Array.isArray(value) || value.version !== 1) {
    throw new Error("配置文件版本无效");
  }
  return validatePairing({deviceId: value.device_id, secret: value.grant_secret});
}

export function isDemoSecret(secret) {
  return secret === DEMO_SECRET;
}

export function maskedSecret(secret) {
  if (!secret) return "--";
  if (secret.length <= 8) return "•".repeat(secret.length);
  return `${secret.slice(0, 4)}${"•".repeat(Math.min(20, secret.length - 8))}${secret.slice(-4)}`;
}

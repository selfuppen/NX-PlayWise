import {chooseUnusedNonce, dayIndexFor, encodeToken, runSelfTest, tierForMinutes, todayUtc8} from "./token.js";
import {
  DEFAULT_CONFIG,
  acknowledgeDemoRisk,
  clearFrontendState,
  demoRiskAcknowledged,
  loadConfig,
  rememberNonce,
  saveConfig,
  usedNoncesFor,
} from "./storage.js";
import {
  isDemoSecret,
  maskedSecret,
  pairingFromFragment,
  pairingFromImportText,
  validatePairing,
} from "./pairing.js";

const form = document.getElementById("generator");
const deviceInput = document.getElementById("device");
const secretInput = document.getElementById("secret");
const dateInput = document.getElementById("date");
const tierInput = document.getElementById("tierMinutes");
const generateButton = document.getElementById("generate");
const result = document.getElementById("result");
const codeOutput = document.getElementById("code");
const metaOutput = document.getElementById("meta");
const errorOutput = document.getElementById("error");
const securityError = document.getElementById("securityError");
const selfTestError = document.getElementById("selfTestError");
const copyButton = document.getElementById("copy");
const installButton = document.getElementById("install");
const demoWarning = document.getElementById("demoWarning");
const importError = document.getElementById("importError");
const importFile = document.getElementById("importFile");
const pairingDialog = document.getElementById("pairingDialog");
const demoDialog = document.getElementById("demoDialog");
let installPrompt = null;

const tierOptions = [1, 2, 3, 4];
for (let minutes = 5; minutes <= 120; minutes += 5) {
  tierOptions.push(minutes);
}
for (const minutes of tierOptions) {
  const option = document.createElement("option");
  option.value = String(minutes);
  option.textContent = `${minutes} 分钟`;
  tierInput.append(option);
}

function showError(target, message) {
  target.textContent = message;
  target.hidden = false;
}

function clearError(target) {
  target.textContent = "";
  target.hidden = true;
}

function syncDemoWarning() {
  demoWarning.hidden = !isDemoSecret(secretInput.value);
}

function dialogDecision(dialog) {
  return new Promise(resolve => {
    dialog.addEventListener("close", () => resolve(dialog.returnValue === "confirm"), {once: true});
    dialog.showModal();
  });
}

async function confirmPairing(pairing, source) {
  document.getElementById("pairingSource").textContent = source;
  document.getElementById("pairingDevice").textContent = pairing.deviceId;
  document.getElementById("pairingSecret").textContent = maskedSecret(pairing.secret);
  const current = loadConfig(localStorage);
  document.getElementById("pairingReplace").hidden =
    current.deviceId === pairing.deviceId && current.secret === pairing.secret;
  if (!await dialogDecision(pairingDialog)) return false;
  deviceInput.value = pairing.deviceId;
  secretInput.value = pairing.secret;
  saveConfig(localStorage, {
    deviceId: pairing.deviceId,
    secret: pairing.secret,
    tierMinutes: Number(tierInput.value),
  });
  syncDemoWarning();
  form.scrollIntoView({behavior: "smooth", block: "start"});
  return true;
}

async function acceptDemoRiskIfNeeded(secret) {
  if (!isDemoSecret(secret) || demoRiskAcknowledged(localStorage)) return true;
  if (!await dialogDecision(demoDialog)) return false;
  acknowledgeDemoRisk(localStorage);
  return true;
}

function resetToDefaults() {
  deviceInput.value = DEFAULT_CONFIG.deviceId;
  secretInput.value = DEFAULT_CONFIG.secret;
  tierInput.value = String(DEFAULT_CONFIG.tierMinutes);
  dateInput.value = todayUtc8();
  syncDemoWarning();
}

function loadSavedForm() {
  const config = loadConfig(localStorage);
  deviceInput.value = config.deviceId;
  secretInput.value = config.secret;
  tierInput.value = String(config.tierMinutes);
  dateInput.value = todayUtc8();
  syncDemoWarning();
}

async function initialize() {
  loadSavedForm();
  let fragmentPairing = null;
  try {
    fragmentPairing = pairingFromFragment(globalThis.location.hash);
  } catch (error) {
    showError(importError, error instanceof Error ? error.message : String(error));
  } finally {
    if (globalThis.location.hash) {
      globalThis.history.replaceState(null, "", `${globalThis.location.pathname}${globalThis.location.search}`);
    }
  }
  if (!globalThis.isSecureContext || !globalThis.crypto?.subtle) {
    showError(securityError, "当前页面无法使用 Web Crypto，请通过 HTTPS 或 localhost 打开。");
    generateButton.disabled = true;
    return;
  }
  try {
    await runSelfTest();
  } catch (error) {
    showError(selfTestError, `加时码算法自检失败，已停止生成：${error instanceof Error ? error.message : String(error)}`);
    generateButton.disabled = true;
    return;
  }
  if ("serviceWorker" in navigator) {
    try {
      await navigator.serviceWorker.register("./sw.js", {scope: "./"});
      document.getElementById("offlineBadge").hidden = false;
    } catch (error) {
      console.warn("Service worker registration failed", error);
    }
  }
  if (fragmentPairing) {
    await confirmPairing(fragmentPairing, "检测到 Switch 二维码中的设备配置，请确认后导入。");
  }
}

form.addEventListener("submit", async event => {
  event.preventDefault();
  generateButton.disabled = true;
  errorOutput.hidden = true;
  result.hidden = true;
  try {
    const deviceId = deviceInput.value.trim();
    const secret = secretInput.value;
    const dateText = dateInput.value;
    const tierMinutes = Number(tierInput.value);
    validatePairing({deviceId, secret});
    if (!await acceptDemoRiskIfNeeded(secret)) return;
    const tierIndex = tierForMinutes(tierMinutes);
    const dayIndex = dayIndexFor(dateText);
    const used = usedNoncesFor(localStorage, deviceId, dateText);
    const nonce = chooseUnusedNonce(used);
    const code = await encodeToken({deviceId, secret, dayIndex, tierIndex, nonce});

    saveConfig(localStorage, {deviceId, secret, tierMinutes});
    rememberNonce(localStorage, deviceId, dateText, nonce);
    codeOutput.textContent = code;
    metaOutput.textContent = `${dateText} · ${tierMinutes} 分钟 · v2`;
    result.hidden = false;
  } catch (error) {
    showError(errorOutput, error instanceof Error ? error.message : String(error));
  } finally {
    if (selfTestError.hidden && securityError.hidden) generateButton.disabled = false;
  }
});

document.getElementById("toggleSecret").addEventListener("click", event => {
  const button = event.currentTarget;
  const visible = secretInput.type === "text";
  secretInput.type = visible ? "password" : "text";
  button.textContent = visible ? "显示" : "隐藏";
  button.setAttribute("aria-pressed", visible ? "false" : "true");
});

secretInput.addEventListener("input", syncDemoWarning);

importFile.addEventListener("change", async () => {
  clearError(importError);
  const [file] = importFile.files || [];
  if (!file) return;
  try {
    if (file.size > 16384) throw new Error("配置文件过大，预期为小于 16 KiB 的 parent-import.json");
    const pairing = pairingFromImportText(await file.text());
    await confirmPairing(pairing, `来自文件：${file.name}`);
  } catch (error) {
    showError(importError, error instanceof Error ? error.message : String(error));
  } finally {
    importFile.value = "";
  }
});

copyButton.addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(codeOutput.textContent || "");
    copyButton.textContent = "已复制";
    setTimeout(() => { copyButton.textContent = "复制加时码"; }, 1500);
  } catch {
    showError(errorOutput, "复制失败，请长按或选中加时码手动复制");
  }
});

document.getElementById("clearConfig").addEventListener("click", () => {
  clearFrontendState(localStorage);
  resetToDefaults();
  result.hidden = true;
  errorOutput.hidden = true;
});

window.addEventListener("beforeinstallprompt", event => {
  event.preventDefault();
  installPrompt = event;
  installButton.hidden = false;
});

installButton.addEventListener("click", async () => {
  if (!installPrompt) return;
  installPrompt.prompt();
  await installPrompt.userChoice;
  installPrompt = null;
  installButton.hidden = true;
});

window.addEventListener("appinstalled", () => {
  installPrompt = null;
  installButton.hidden = true;
});

initialize();

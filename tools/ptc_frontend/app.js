import {chooseUnusedNonce, dayIndexFor, encodeToken, runSelfTest, tierForMinutes, todayLocal} from "./token.js";
import {
  DEFAULT_CONFIG,
  acknowledgeDemoRisk,
  clearFrontendState,
  demoRiskAcknowledged,
  loadConfig,
  loadLanguage,
  rememberNonce,
  saveConfig,
  saveLanguage,
  usedNoncesFor,
} from "./storage.js";
import {
  isDemoSecret,
  maskedSecret,
  pairingFromFragment,
  pairingFromImportText,
  validatePairing,
} from "./pairing.js";
import {
  DEFAULT_LANG,
  SUPPORTED_LANGS,
  detectBrowserLanguage,
  t,
} from "./i18n.js";

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
const compatibilityWarning = document.getElementById("compatibilityWarning");
const langToggle = document.getElementById("langToggle");
const standaloneMode = document.documentElement.dataset.standalone === "true";
let installPrompt = null;

let lastResultDate = "";
let lastResultMinutes = 0;

function createMemoryStorage() {
  const values = new Map();
  return {
    getItem(key) { return values.has(key) ? values.get(key) : null; },
    setItem(key, value) { values.set(key, String(value)); },
    removeItem(key) { values.delete(key); },
  };
}

function selectStorage() {
  try {
    const storage = globalThis.localStorage;
    const probeKey = "ptc.frontend.storage-probe";
    storage.setItem(probeKey, "ok");
    storage.removeItem(probeKey);
    return {storage, persistent: true};
  } catch {
    return {storage: createMemoryStorage(), persistent: false};
  }
}

const selectedStorage = selectStorage();
const frontendStorage = selectedStorage.storage;

let currentLang = loadLanguage(frontendStorage) || detectBrowserLanguage();
if (!SUPPORTED_LANGS.includes(currentLang)) currentLang = DEFAULT_LANG;

const tierOptions = [1, 2, 3, 4];
for (let minutes = 5; minutes <= 120; minutes += 5) {
  tierOptions.push(minutes);
}
tierOptions.push(150, 180, 210, 240);
for (const minutes of tierOptions) {
  const option = document.createElement("option");
  option.value = String(minutes);
  option.textContent = t("minutes_unit", {m: minutes}, currentLang);
  tierInput.append(option);
}

function applyTranslations(lang) {
  currentLang = lang;
  saveLanguage(frontendStorage, lang);
  document.documentElement.lang = lang === "zh" ? "zh-CN" : "en";

  document.querySelectorAll("[data-i18n]").forEach(element => {
    const key = element.dataset.i18n;
    if (!key) return;
    if (key === "intro_p") {
      element.textContent = t(standaloneMode ? "intro_p_standalone" : "intro_p_online", {}, lang);
    } else if (key === "badge_offline") {
      element.textContent = t(standaloneMode ? "badge_standalone" : "badge_offline", {}, lang);
    } else if (key === "footer_p") {
      element.textContent = t(standaloneMode ? "footer_p_standalone" : "footer_p_online", {}, lang);
    } else if (key === "btn_toggle_secret_show" || key === "btn_toggle_secret_hide") {
      const visible = secretInput.type === "text";
      element.textContent = t(visible ? "btn_toggle_secret_hide" : "btn_toggle_secret_show", {}, lang);
    } else if (key === "btn_copy" || key === "btn_copied") {
      element.textContent = t(copyButton.dataset.copied === "true" ? "btn_copied" : "btn_copy", {}, lang);
    } else {
      element.textContent = t(key, {}, lang);
    }
  });

  document.querySelectorAll("[data-i18n-html]").forEach(element => {
    const key = element.dataset.i18nHtml;
    if (!key) return;
    if (key === "pairing_p") {
      element.innerHTML = t(standaloneMode ? "pairing_p_standalone" : "pairing_p_online", {}, lang);
    } else {
      element.innerHTML = t(key, {}, lang);
    }
  });

  document.querySelectorAll("[data-i18n-aria]").forEach(element => {
    const key = element.dataset.i18nAria;
    if (key) element.setAttribute("aria-label", t(key, {}, lang));
  });

  if (langToggle) {
    langToggle.textContent = t("lang_toggle_btn", {}, lang);
    langToggle.setAttribute("aria-label", t("lang_toggle_aria", {}, lang));
  }

  const currentTier = tierInput.value;
  for (const option of tierInput.options) {
    const mins = Number(option.value);
    option.textContent = t("minutes_unit", {m: mins}, lang);
  }
  tierInput.value = currentTier;

  if (!result.hidden && lastResultDate && lastResultMinutes) {
    metaOutput.textContent = t("result_meta", {date: lastResultDate, minutes: lastResultMinutes}, lang);
  }
}

if (langToggle) {
  langToggle.addEventListener("click", () => {
    const nextLang = currentLang === "zh" ? "en" : "zh";
    applyTranslations(nextLang);
  });
}

function showError(target, message) {
  target.textContent = message;
  target.hidden = false;
}

function clearError(target) {
  target.textContent = "";
  target.hidden = true;
}

function addCompatibilityWarning(message) {
  const item = document.createElement("p");
  item.textContent = message;
  compatibilityWarning.append(item);
  compatibilityWarning.hidden = false;
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
  const current = loadConfig(frontendStorage);
  document.getElementById("pairingReplace").hidden =
    current.deviceId === pairing.deviceId && current.secret === pairing.secret;
  if (!await dialogDecision(pairingDialog)) return false;
  deviceInput.value = pairing.deviceId;
  secretInput.value = pairing.secret;
  saveConfig(frontendStorage, {
    deviceId: pairing.deviceId,
    secret: pairing.secret,
    tierMinutes: Number(tierInput.value),
  });
  syncDemoWarning();
  form.scrollIntoView({behavior: "smooth", block: "start"});
  return true;
}

async function acceptDemoRiskIfNeeded(secret) {
  if (!isDemoSecret(secret) || demoRiskAcknowledged(frontendStorage)) return true;
  if (!await dialogDecision(demoDialog)) return false;
  acknowledgeDemoRisk(frontendStorage);
  return true;
}

function resetToDefaults() {
  deviceInput.value = DEFAULT_CONFIG.deviceId;
  secretInput.value = DEFAULT_CONFIG.secret;
  tierInput.value = String(DEFAULT_CONFIG.tierMinutes);
  dateInput.value = todayLocal();
  syncDemoWarning();
}

function loadSavedForm() {
  const config = loadConfig(frontendStorage);
  deviceInput.value = config.deviceId;
  secretInput.value = config.secret;
  tierInput.value = String(config.tierMinutes);
  dateInput.value = todayLocal();
  syncDemoWarning();
}

async function initialize() {
  loadSavedForm();
  applyTranslations(currentLang);
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
  if (typeof TextEncoder !== "function" || typeof DataView !== "function") {
    showError(securityError, t("err_browser_capability", {}, currentLang));
    generateButton.disabled = true;
    return;
  }
  const hasSecureRandom = typeof globalThis.crypto?.getRandomValues === "function";
  if (!standaloneMode && (!globalThis.isSecureContext || !globalThis.crypto?.subtle || !hasSecureRandom)) {
    showError(securityError, t("err_web_crypto", {}, currentLang));
    generateButton.disabled = true;
    return;
  }
  if (standaloneMode && !globalThis.crypto?.subtle) {
    addCompatibilityWarning(t("warn_compat_subtle", {}, currentLang));
  }
  if (standaloneMode && !hasSecureRandom) {
    addCompatibilityWarning(t("warn_compat_random", {}, currentLang));
  }
  if (!selectedStorage.persistent) {
    addCompatibilityWarning(t("warn_compat_storage", {}, currentLang));
  }
  try {
    await runSelfTest();
  } catch (error) {
    showError(selfTestError, `${t("err_selftest", {}, currentLang)}${error instanceof Error ? error.message : String(error)}`);
    generateButton.disabled = true;
    return;
  }
  if (!standaloneMode && "serviceWorker" in navigator) {
    try {
      await navigator.serviceWorker.register("./sw.js", {scope: "./"});
      document.getElementById("offlineBadge").hidden = false;
    } catch (error) {
      console.warn("Service worker registration failed", error);
    }
  }
  if (fragmentPairing) {
    await confirmPairing(fragmentPairing, t("pairing_source_qr", {}, currentLang));
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
    const used = usedNoncesFor(frontendStorage, deviceId, dateText);
    const nonce = chooseUnusedNonce(used);
    const code = await encodeToken({deviceId, secret, dayIndex, tierIndex, nonce});

    saveConfig(frontendStorage, {deviceId, secret, tierMinutes});
    rememberNonce(frontendStorage, deviceId, dateText, nonce);
    codeOutput.textContent = code;
    lastResultDate = dateText;
    lastResultMinutes = tierMinutes;
    metaOutput.textContent = t("result_meta", {date: dateText, minutes: tierMinutes}, currentLang);
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
  button.textContent = t(visible ? "btn_toggle_secret_show" : "btn_toggle_secret_hide", {}, currentLang);
  button.setAttribute("aria-pressed", visible ? "false" : "true");
});

secretInput.addEventListener("input", syncDemoWarning);

importFile.addEventListener("change", async () => {
  clearError(importError);
  const [file] = importFile.files || [];
  if (!file) return;
  try {
    if (file.size > 16384) throw new Error(t("err_import_size", {}, currentLang));
    const pairing = pairingFromImportText(await file.text());
    await confirmPairing(pairing, t("pairing_source_file", {name: file.name}, currentLang));
  } catch (error) {
    showError(importError, error instanceof Error ? error.message : String(error));
  } finally {
    importFile.value = "";
  }
});

copyButton.addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(codeOutput.textContent || "");
    copyButton.dataset.copied = "true";
    copyButton.textContent = t("btn_copied", {}, currentLang);
    setTimeout(() => {
      copyButton.dataset.copied = "false";
      copyButton.textContent = t("btn_copy", {}, currentLang);
    }, 1500);
  } catch {
    showError(errorOutput, t("err_copy_failed", {}, currentLang));
  }
});

document.getElementById("clearConfig").addEventListener("click", () => {
  clearFrontendState(frontendStorage);
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

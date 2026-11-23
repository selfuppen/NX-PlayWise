import {chooseUnusedNonce, dayIndexFor, encodeToken, runSelfTest, tierForMinutes, todayLocal} from "./token.js";
import {
  DEFAULT_CONFIG,
  acknowledgeDemoRisk,
  clearFrontendState,
  demoRiskAcknowledged,
  issuedFor,
  loadConfig,
  loadDevices,
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

const toolRoot = document.getElementById("toolRoot");
const form = document.getElementById("generator");
const deviceInput = document.getElementById("device");
const secretInput = document.getElementById("secret");
const dateInput = document.getElementById("date");
const tierInput = document.getElementById("tierMinutes");
const generateButton = document.getElementById("generate");
const result = document.getElementById("result");
const resultHeading = document.getElementById("resultHeading");
const codeOutput = document.getElementById("code");
const metaOutput = document.getElementById("meta");
const errorOutput = document.getElementById("error");
const securityError = document.getElementById("securityError");
const selfTestError = document.getElementById("selfTestError");
const copyButton = document.getElementById("copy");
const regenerateButton = document.getElementById("regenerate");
const copyMessage = document.getElementById("copyMessage");
const headerDeviceBtn = document.getElementById("headerDeviceBtn");
const headerDeviceName = document.getElementById("headerDeviceName");
const openDrawerBtn = document.getElementById("openDrawerBtn");
const introPairBtn = document.getElementById("introPairBtn");
const demoPairCta = document.getElementById("demoPairCta");
const pairingDrawer = document.getElementById("pairingDrawer");
const pairingDrawerBackdrop = document.getElementById("pairingDrawerBackdrop");
const closeDrawerBtn = document.getElementById("closeDrawerBtn");
const pairedBar = document.getElementById("pairedBar");
const pairedDeviceName = document.getElementById("pairedDeviceName");
const pairedStatusBadge = document.getElementById("pairedStatusBadge");
const devicePicker = document.getElementById("devicePicker");
const pairingSettings = document.getElementById("pairingSettings");
const dateShortcuts = document.querySelectorAll("#dateShortcuts button");
const tierShortcuts = document.getElementById("tierShortcuts");
const issuedHistory = document.getElementById("issuedHistory");
const issuedSummary = document.getElementById("issuedSummary");
const issuedLatest = document.getElementById("issuedLatest");
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
let lastResultDevice = "";
let generating = false;

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

for (const minutes of [15, 30, 45, 60, 90, 120]) {
  const button = document.createElement("button");
  button.type = "button";
  button.className = "tier-chip";
  button.value = String(minutes);
  button.addEventListener("click", () => {
    tierInput.value = button.value;
    refreshShortcuts();
  });
  tierShortcuts.append(button);
}

function setDrawerOpen(open) {
  if (!pairingDrawer) return;
  pairingDrawer.hidden = !open;
  document.body.classList.toggle("drawer-open", open);
  headerDeviceBtn?.setAttribute("aria-expanded", String(open));
  openDrawerBtn?.setAttribute("aria-expanded", String(open));
  if (open) {
    closeDrawerBtn?.focus();
  }
}

function localDateAfter(days) {
  const date = new Date();
  date.setDate(date.getDate() + days);
  return todayLocal(date.getTime());
}

function refreshShortcuts() {
  for (const button of dateShortcuts) {
    button.setAttribute("aria-pressed", String(dateInput.value === localDateAfter(Number(button.dataset.days))));
  }
  for (const button of tierShortcuts.children) {
    button.textContent = `+${t("minutes_unit", {m: Number(button.value)}, currentLang)}`;
    button.setAttribute("aria-pressed", String(tierInput.value === button.value));
  }
  const mins = Number(tierInput.value);
  if (Number.isInteger(mins) && mins > 0) {
    generateButton.textContent = t("btn_generate_minutes", {m: mins}, currentLang);
  }
}

function refreshDevices(collapse = false) {
  const devices = loadDevices(frontendStorage);
  const activeId = deviceInput.value.trim();
  const isPaired = devices.has(activeId);
  const demo = isDemoSecret(secretInput.value);
  devicePicker.replaceChildren();
  for (const id of devices.keys()) devicePicker.add(new Option(id, id));
  devicePicker.value = activeId;
  devicePicker.closest("label").hidden = devices.size <= 1;
  pairedBar.hidden = !isPaired;
  toolRoot?.classList.toggle("is-paired", isPaired && !demo);

  if (headerDeviceName) {
    headerDeviceName.textContent = activeId || t("unpaired_label", {}, currentLang);
  }
  if (headerDeviceBtn) {
    headerDeviceBtn.classList.toggle("is-unpaired", !isPaired || demo);
  }
  if (isPaired) {
    pairedDeviceName.textContent = activeId;
    pairedStatusBadge.textContent = t(demo ? "demo_badge" : "paired_badge", {}, currentLang);
    pairedStatusBadge.classList.toggle("is-demo", demo);
  }
  if (collapse) {
    pairingSettings.open = !isPaired || demo;
  }
}

function refreshHistory() {
  const records = issuedFor(frontendStorage, deviceInput.value.trim(), dateInput.value);
  const list = document.getElementById("issuedList");
  list.replaceChildren();
  issuedHistory.hidden = records.length === 0;
  if (records.length > 0) {
    issuedSummary.textContent = t("issued_summary_count", {n: records.length}, currentLang);
    if (issuedLatest) {
      const latest = records[0];
      issuedLatest.textContent = `····-${latest.suffix} (+${latest.minutes}m)`;
    }
  } else if (issuedLatest) {
    issuedLatest.textContent = "";
  }
  const clock = new Intl.DateTimeFormat(currentLang === "zh" ? "zh-CN" : "en", {hour: "numeric", minute: "2-digit"});
  for (const record of records) {
    const row = document.createElement("li");
    row.className = "history-row";
    const meta = document.createElement("span");
    meta.className = "history-row-meta";
    const timeEl = document.createElement("strong");
    timeEl.textContent = clock.format(record.time);
    const minEl = document.createElement("span");
    minEl.className = "history-min-pill";
    minEl.textContent = `+${t("minutes_unit", {m: record.minutes}, currentLang)}`;
    meta.append(timeEl, minEl);
    const suffixEl = document.createElement("code");
    suffixEl.className = "history-suffix";
    suffixEl.textContent = `···· - ${record.suffix}`;
    row.append(meta, suffixEl);
    list.append(row);
  }
}

function refreshIssueDate() {
  result.hidden = true;
  refreshShortcuts();
  refreshHistory();
}

function updateResultCopy() {
  if (result.hidden || !lastResultDate || !lastResultMinutes) return;
  if (resultHeading) {
    resultHeading.textContent = t("result_ready_title", {minutes: lastResultMinutes}, currentLang);
  }
  metaOutput.textContent = t("result_meta_device", {
    device: lastResultDevice, date: lastResultDate, minutes: lastResultMinutes,
  }, currentLang);
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
    } else if (key === "btn_copy" || key === "btn_copy_message") {
      element.textContent = t(element.dataset.copied === "true" ? "btn_copied" : key, {}, lang);
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
    const textNode = langToggle.querySelector("[data-i18n='lang_toggle_btn']");
    if (textNode) {
      textNode.textContent = t("lang_toggle_btn", {}, lang);
    } else {
      langToggle.textContent = t("lang_toggle_btn", {}, lang);
    }
    langToggle.setAttribute("aria-label", t("lang_toggle_aria", {}, lang));
  }

  const currentTier = tierInput.value;
  for (const option of tierInput.options) {
    const mins = Number(option.value);
    option.textContent = t("minutes_unit", {m: mins}, lang);
  }
  tierInput.value = currentTier;

  updateResultCopy();
  refreshDevices();
  refreshShortcuts();
  refreshHistory();
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
    dialog.returnValue = "";
    dialog.addEventListener("close", () => resolve(dialog.returnValue === "confirm"), {once: true});
    dialog.showModal();
  });
}

async function confirmPairing(pairing, source) {
  document.getElementById("pairingSource").textContent = source;
  document.getElementById("pairingDevice").textContent = pairing.deviceId;
  document.getElementById("pairingSecret").textContent = maskedSecret(pairing.secret);
  const current = loadDevices(frontendStorage).get(pairing.deviceId);
  document.getElementById("pairingReplace").hidden = !current || current.secret === pairing.secret;
  if (!await dialogDecision(pairingDialog)) return false;
  deviceInput.value = pairing.deviceId;
  secretInput.value = pairing.secret;
  saveConfig(frontendStorage, {
    deviceId: pairing.deviceId,
    secret: pairing.secret,
    tierMinutes: current?.tierMinutes ?? DEFAULT_CONFIG.tierMinutes,
  });
  tierInput.value = String(current?.tierMinutes ?? DEFAULT_CONFIG.tierMinutes);
  hideSecret();
  syncDemoWarning();
  refreshDevices(true);
  refreshIssueDate();
  setDrawerOpen(false);
  form.scrollIntoView({behavior: "smooth", block: "start"});
  return true;
}

async function acceptDemoRiskIfNeeded(secret) {
  if (!isDemoSecret(secret) || demoRiskAcknowledged(frontendStorage)) return true;
  if (!await dialogDecision(demoDialog)) return false;
  acknowledgeDemoRisk(frontendStorage);
  return true;
}

function showDevice(config) {
  deviceInput.value = config.deviceId;
  secretInput.value = config.secret;
  tierInput.value = String(config.tierMinutes);
  hideSecret();
  syncDemoWarning();
  refreshDevices(true);
  refreshIssueDate();
}

function hideSecret() {
  secretInput.type = "password";
  const button = document.getElementById("toggleSecret");
  button.textContent = t("btn_toggle_secret_show", {}, currentLang);
  button.setAttribute("aria-pressed", "false");
}

async function initialize() {
  dateInput.value = todayLocal();
  showDevice(loadConfig(frontendStorage));
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

async function handleGenerate() {
  if (generating || generateButton.disabled) return;
  generating = true;
  form.inert = true;
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
    const issue = async () => {
      saveConfig(frontendStorage, {deviceId, secret, tierMinutes});
      for (;;) {
        const nonce = chooseUnusedNonce(usedNoncesFor(frontendStorage, deviceId, dateText));
        const code = await encodeToken({deviceId, secret, dayIndex, tierIndex, nonce});
        const receipt = {time: Date.now(), minutes: tierMinutes, suffix: code.slice(4)};
        if (rememberNonce(frontendStorage, deviceId, dateText, nonce, receipt)) return code;
      }
    };
    const code = navigator.locks
      ? await navigator.locks.request("playwise.issue", issue)
      : await issue();

    codeOutput.children[0].textContent = code.slice(0, 4);
    codeOutput.children[1].textContent = code.slice(4);
    lastResultDate = dateText;
    lastResultMinutes = tierMinutes;
    lastResultDevice = deviceId;
    refreshDevices(true);
    refreshHistory();
    result.hidden = false;
    updateResultCopy();
    result.focus();
  } catch (error) {
    showError(errorOutput, error instanceof Error ? error.message : String(error));
  } finally {
    generating = false;
    form.inert = false;
    if (selfTestError.hidden && securityError.hidden) generateButton.disabled = false;
  }
}

form.addEventListener("submit", async event => {
  event.preventDefault();
  await handleGenerate();
});

regenerateButton?.addEventListener("click", () => {
  form.requestSubmit();
});

document.getElementById("toggleSecret").addEventListener("click", event => {
  const button = event.currentTarget;
  const visible = secretInput.type === "text";
  secretInput.type = visible ? "password" : "text";
  button.textContent = t(visible ? "btn_toggle_secret_show" : "btn_toggle_secret_hide", {}, currentLang);
  button.setAttribute("aria-pressed", visible ? "false" : "true");
});

secretInput.addEventListener("input", () => {
  syncDemoWarning();
  refreshDevices();
  result.hidden = true;
});

for (const trigger of [headerDeviceBtn, openDrawerBtn, introPairBtn, demoPairCta]) {
  trigger?.addEventListener("click", () => {
    pairingSettings.open = true;
    setDrawerOpen(true);
  });
}
closeDrawerBtn?.addEventListener("click", () => setDrawerOpen(false));
pairingDrawerBackdrop?.addEventListener("click", () => setDrawerOpen(false));
window.addEventListener("keydown", event => {
  if (event.key === "Escape" && pairingDrawer && !pairingDrawer.hidden) {
    setDrawerOpen(false);
  }
});

form.addEventListener("invalid", event => {
  if (pairingDrawer && pairingDrawer.contains(event.target)) {
    pairingSettings.open = true;
    setDrawerOpen(true);
  }
}, true);

deviceInput.addEventListener("input", () => {
  refreshDevices();
  refreshIssueDate();
});
devicePicker.addEventListener("change", () => {
  const config = loadDevices(frontendStorage).get(devicePicker.value);
  if (!config) return;
  saveConfig(frontendStorage, config);
  showDevice(config);
});
dateInput.addEventListener("input", refreshIssueDate);
tierInput.addEventListener("change", refreshShortcuts);
for (const button of dateShortcuts) {
  button.addEventListener("click", () => {
    dateInput.value = localDateAfter(Number(button.dataset.days));
    refreshIssueDate();
  });
}

for (const [id, values] of Object.entries({
  appearance: ["slate", "paper", "teal", "dusk"],
  colorMode: ["system", "light", "dark"],
})) {
  const picker = document.getElementById(id);
  const key = `ptc.frontend.${id}.v1`;
  const saved = frontendStorage.getItem(key);
  picker.value = values.includes(saved) ? saved : values[0];
  const systemDark = id === "colorMode" ? matchMedia("(prefers-color-scheme: dark)") : null;
  const apply = () => {
    document.documentElement.dataset[id] = systemDark && picker.value === "system"
      ? (systemDark.matches ? "dark" : "light")
      : picker.value;
  };
  apply();
  systemDark?.addEventListener?.("change", apply);
  picker.addEventListener("change", () => {
    apply();
    frontendStorage.setItem(key, picker.value);
  });
}

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

async function copyResult(button, value) {
  try {
    await navigator.clipboard.writeText(value);
    button.dataset.copied = "true";
    button.textContent = t("btn_copied", {}, currentLang);
    clearTimeout(Number(button.dataset.copyTimer));
    button.dataset.copyTimer = setTimeout(() => {
      button.dataset.copied = "false";
      button.textContent = t(button.dataset.i18n, {}, currentLang);
    }, 1500);
  } catch {
    showError(errorOutput, t("err_copy_failed", {}, currentLang));
  }
}

copyButton.addEventListener("click", () => copyResult(copyButton, codeOutput.textContent));
copyMessage.addEventListener("click", () => {
  copyResult(copyMessage, t("share_message", {
    code: codeOutput.textContent,
    device: lastResultDevice,
    date: lastResultDate,
    minutes: lastResultMinutes,
  }, currentLang));
});

document.getElementById("clearConfig").addEventListener("click", () => {
  if (generating) return;
  clearFrontendState(frontendStorage);
  dateInput.value = todayLocal();
  showDevice(DEFAULT_CONFIG);
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

/* Optional real-browser release matrix. Supply the installed Playwright module
 * directory explicitly; the normal stdlib Host entry does not install browsers. */
import assert from "node:assert/strict";
import {mkdir, readFile, writeFile, copyFile, rm} from "node:fs/promises";
import path from "node:path";
import {pathToFileURL} from "node:url";
import {createHash} from "node:crypto";
import {dayIndexFor, encodeToken, tierForMinutes} from "../../tools/ptc_frontend/token.js";

const [moduleRoot, origin, out, runtimeRoot] = process.argv.slice(2);
assert(moduleRoot && origin && out && runtimeRoot,
  "node pwa_matrix.mjs <node_modules> <loopback-origin> <new-output> <new-profile-root>");
assert(["127.0.0.1", "localhost"].includes(new URL(origin).hostname), "isolated loopback server required");
await mkdir(out, {recursive: false});
await mkdir(runtimeRoot, {recursive: false});
await mkdir(path.join(out, "screens"));
await mkdir(path.join(out, "artifacts"));
await copyFile(new URL(import.meta.url), path.join(out, "artifacts/pwa_matrix.mjs"));
const {chromium} = await import(pathToFileURL(path.join(moduleRoot, "playwright/index.mjs")).href);
const browser = await chromium.launch({channel: "chrome", headless: true});
const pairing = {version: 1, device_id: "eden-switch", grant_secret: "playwise-eden-test-secret-00000001"};
const date = "2026-10-25";
const report = {schema_version: 1, browser: browser.version(), origin, date, started_at: new Date().toISOString(),
  scope: "real desktop Chrome and Android viewport emulation; physical install and optical scan recorded separately",
  source_assets: [], cases: [], errors: []};
const sha = bytes => createHash("sha256").update(bytes).digest("hex");
const assets = ["index.html", "styles.css", "app.js", "token.js", "storage.js", "pairing.js", "i18n.js",
  "sw.js", "manifest.webmanifest", "icon.svg", "icon-maskable.svg", "playwise-offline.html"];
const sourceRoot = new URL("../../tools/ptc_frontend/", import.meta.url);
for (const name of assets) {
  const bytes = await readFile(new URL(name, sourceRoot));
  await writeFile(path.join(out, "artifacts", name), bytes);
  report.source_assets.push({path: name, sha256: sha(bytes), bytes: bytes.length});
}
async function capture(tab, name) {
  const bytes = await tab.screenshot({fullPage: true});
  await writeFile(path.join(out, "screens", name + ".png"), bytes);
  return {path: "screens/" + name + ".png", sha256: sha(bytes)};
}
async function issue(tab) {
  await tab.locator("#date").fill(date);
  await tab.locator("#tierMinutes").selectOption("30");
  const before = await tab.evaluate(() => JSON.parse(localStorage.getItem("ptc.frontend.nonces.v1") || "{}"));
  await tab.locator("#generate").click();
  await tab.locator("#result:not([hidden])").waitFor();
  const code = (await tab.locator("#code").textContent()).trim();
  assert.match(code, /^\d{8}$/);
  const after = await tab.evaluate(() => JSON.parse(localStorage.getItem("ptc.frontend.nonces.v1")));
  const key = pairing.device_id + "\0" + date;
  const added = after[key].filter(nonce => !(before[key] || []).includes(nonce));
  assert.equal(added.length, 1);
  const [nonce] = added;
  assert.equal(after[key].length, (before[key] || []).length + 1);
  assert.equal(new Set(after[key]).size, after[key].length);
  assert.equal(code, await encodeToken({deviceId: pairing.device_id, secret: pairing.grant_secret,
    dayIndex: dayIndexFor(date), tierIndex: tierForMinutes(30), nonce}));
  assert.equal(await tab.locator("#secret").getAttribute("type"), "password");
  return {code, nonce, used: after[key]};
}
try {
  const cold = await browser.newContext();
  try {
    await cold.setOffline(true);
    const tab = await cold.newPage();
    let failed = false;
    try { await tab.goto(origin, {waitUntil: "load", timeout: 10000}); } catch { failed = true; }
    assert(failed, "fresh offline context unexpectedly loaded the network app");
    report.cold_offline = {network_app_unavailable: failed, expected: true};
  } finally { await cold.close(); }
  for (const mobile of [false, true]) for (const locale of ["zh-CN", "en-US"]) for (const colorScheme of ["light", "dark"]) {
    const name = `${mobile ? "android-emulated" : "desktop"}-${locale}-${colorScheme}`;
    const profile = path.resolve(runtimeRoot, name);
    const context = await chromium.launchPersistentContext(profile, {channel: "chrome", headless: true,
      locale, colorScheme, timezoneId: "Asia/Shanghai",
      viewport: mobile ? {width: 412, height: 915} : {width: 1280, height: 900},
      isMobile: mobile, hasTouch: mobile, deviceScaleFactor: mobile ? 2.625 : 1});
    const external = [], errors = [];
    context.on("request", req => { if (new URL(req.url()).origin !== origin) external.push(req.url()); });
    context.on("page", tab => tab.on("pageerror", error => errors.push(String(error))));
    const row = {name, profile, mobile_emulation: mobile, locale, colorScheme, status: "running"};
    report.cases.push(row);
    try {
      let tab = await context.newPage();
      await tab.goto(origin + "/#device_id=" + pairing.device_id + "&grant_secret=" + pairing.grant_secret);
      await tab.locator("#pairingDialog[open]").waitFor();
      assert.equal(await tab.locator("#pairingDevice").textContent(), pairing.device_id);
      assert.equal(new URL(tab.url()).hash, "");
      await tab.locator('#pairingDialog button[value="confirm"]').click();
      await tab.waitForFunction(() => !!navigator.serviceWorker.controller);
      await tab.evaluate(() => navigator.serviceWorker.ready);
      row.online = await issue(tab);
      row.online_screenshot = await capture(tab, name + "-online");
      const client = await context.newCDPSession(tab);
      const {installabilityErrors} = await client.send("Page.getInstallabilityErrors");
      row.installability_errors = installabilityErrors;
      assert.deepEqual(installabilityErrors, [], "Chrome installation criteria failed");
      await client.detach();
      row.cache = await tab.evaluate(async () => {
        const keys = await caches.keys();
        return {keys, urls: (await (await caches.open(keys[0])).keys()).map(r => r.url)};
      });
      const expectedCache = ["/", ...assets.filter(name => name !== "sw.js").map(name => "/" + name)]
        .map(name => origin + name).sort();
      assert.deepEqual([...row.cache.urls].sort(), expectedCache);
      await context.setOffline(true);
      await tab.close();
      tab = await context.newPage();
      await tab.goto(origin + "/", {waitUntil: "load"});
      await tab.locator("#generate").waitFor();
      assert.equal(await tab.locator("#device").inputValue(), pairing.device_id);
      row.offline = await issue(tab);
      assert(row.online.used.every(nonce => row.offline.used.includes(nonce)));
      row.offline_screenshot = await capture(tab, name + "-offline");
      await tab.reload({waitUntil: "load"});
      row.after_offline_reload = await issue(tab);
      assert(row.offline.used.every(nonce => row.after_offline_reload.used.includes(nonce)));
      const single = await tab.evaluate(async () => (await fetch("./playwise-offline.html")).text());
      assert.equal(sha(Buffer.from(single)), report.source_assets.find(v => v.path === "playwise-offline.html").sha256);
      row.standalone_cached = true;
      assert.deepEqual(errors, []);
      assert.deepEqual(external, []);
      row.external_requests = external;
      row.page_errors = errors;
      row.status = "pass";
    } catch (error) {
      row.status = "failed";
      row.error = String(error.stack || error);
      throw error;
    } finally {
      await context.close();
      if (row.status === "pass") {
        assert(path.dirname(profile) === path.resolve(runtimeRoot));
        await rm(profile, {recursive: true});
        row.profile_removed_after_close = true;
      }
    }
  }
  // Single-file fallback starts with no service worker or network and imports
  // the fixture through the actual file input, independently of PWA caches.
  const context = await browser.newContext();
  try {
    await context.setOffline(true);
    const tab = await context.newPage();
    await tab.goto(pathToFileURL(path.resolve(out, "artifacts/playwise-offline.html")).href);
    await tab.locator("#generate").waitFor();
    await tab.locator("#importFile").setInputFiles({name: "parent-import.json", mimeType: "application/json",
      buffer: Buffer.from(JSON.stringify(pairing))});
    await tab.locator("#pairingDialog[open]").waitFor();
    await tab.locator('#pairingDialog button[value="confirm"]').click();
    report.standalone_cold_offline = await issue(tab);
    report.standalone_screenshot = await capture(tab, "standalone-cold-offline");
  } finally { await context.close(); }
  report.status = "pass";
} catch (error) {
  report.errors.push(String(error.stack || error));
  report.status = "partial";
  process.exitCode = 1;
} finally {
  await browser.close();
  report.contexts_closed = true;
  report.finished_at = new Date().toISOString();
  await writeFile(path.join(out, "result.json"), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({status: report.status, browser: report.browser, cases: report.cases.length, errors: report.errors}));
}

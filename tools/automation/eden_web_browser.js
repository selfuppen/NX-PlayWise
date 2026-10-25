/* Run through Playwright after substituting the current loopback handoff origin below. */
async (page) => {
  const origin = "__EDEN_ORIGIN__";
  const browser = page.context().browser();
  const context = await browser.newContext({locale: "zh-CN", timezoneId: "Asia/Shanghai"});
  const tab = await context.newPage();
  const external = [];
  try {
    context.on("request", request => {
      if (new URL(request.url()).origin !== origin) external.push(new URL(request.url()).origin);
    });
    const inputReply = await context.request.get(origin + "/__eden_test/request");
    if (!inputReply.ok()) throw new Error("handoff request unavailable");
    const request = await inputReply.json();
    const exported = await context.request.get(origin + "/__eden_test/parent-import.json");
    const bytes = await exported.body();
    const pairing = JSON.parse(bytes.toString("utf8"));
    await tab.goto(origin + "/", {waitUntil: "networkidle"});
    await tab.locator("#generate").waitFor({state: "visible"});
    await tab.locator("#importFile").setInputFiles({name: "parent-import.json", mimeType: "application/json", buffer: bytes});
    await tab.locator("#pairingDialog[open]").waitFor();
    if ((await tab.locator("#pairingDevice").textContent()) !== pairing.device_id) throw new Error("wrong imported device");
    await tab.locator('#pairingDialog button[value="confirm"]').click();
    await tab.locator("#pairingDialog").waitFor({state: "hidden"});
    await tab.reload({waitUntil: "networkidle"});
    const persisted = await tab.evaluate(pairing => {
      const saved = JSON.parse(localStorage.getItem("ptc.frontend.config.v1"));
      return saved.deviceId === pairing.device_id && saved.secret === pairing.grant_secret &&
        document.querySelector("#device").value === pairing.device_id && document.querySelector("#secret").value === pairing.grant_secret;
    }, pairing);
    if (!persisted) throw new Error("pairing did not persist");
    await tab.locator("#date").fill(request.date);
    await tab.locator("#tierMinutes").selectOption("30");
    await tab.locator("#generate").click();
    await tab.locator("#result:not([hidden])").waitFor();
    const code = (await tab.locator("#code").textContent()).trim();
    if (!/^\d{8}$/.test(code)) throw new Error("not an eight-digit DOM code");
    const nonce = await tab.evaluate(({device, date}) => {
      const state = JSON.parse(localStorage.getItem("ptc.frontend.nonces.v1"));
      const used = state[device + "\0" + date];
      if (!Array.isArray(used) || used.length !== 1) throw new Error("fresh browser has unexpected nonces");
      return used[0];
    }, {device: pairing.device_id, date: request.date});
    const png = await tab.screenshot({fullPage: true});
    const sha256 = await tab.evaluate(async bytes => {
      const hash = await crypto.subtle.digest("SHA-256", Uint8Array.from(bytes));
      return [...new Uint8Array(hash)].map(v => v.toString(16).padStart(2, "0")).join("");
    }, Array.from(png));
    const screenshot = await context.request.post(origin + "/__eden_test/screenshot", {
      headers: {"X-PlayWise-Run": request.run_id, "Content-Type": "image/png"}, data: png
    });
    if (!screenshot.ok()) throw new Error("screenshot handoff rejected");
    await tab.reload({waitUntil: "networkidle"});
    const nonceRetained = await tab.evaluate(({device, date, nonce}) => {
      const state = JSON.parse(localStorage.getItem("ptc.frontend.nonces.v1"));
      const config = JSON.parse(localStorage.getItem("ptc.frontend.config.v1"));
      return state[device + "\0" + date].length === 1 && state[device + "\0" + date][0] === nonce && config.tierMinutes === 30;
    }, {device: pairing.device_id, date: request.date, nonce});
    const result = {schema_version: 1, run_id: request.run_id, status: "pass", browser: browser.version(),
      device_id: pairing.device_id, date: request.date, day_index: request.day_index, minutes: 30, nonce, code,
      import_confirmed: true, pairing_persisted_after_reload: persisted, nonce_persisted_after_reload: nonceRetained,
      external_requests: external, screenshot_sha256: sha256};
    const reply = await context.request.post(origin + "/__eden_test/result", {
      headers: {"X-PlayWise-Run": request.run_id}, data: result
    });
    if (!reply.ok()) throw new Error("result handoff rejected; no retry");
    return {status: "pass", browser: result.browser, device_id: result.device_id,
      date: result.date, minutes: result.minutes, nonce: result.nonce, storage_persisted: nonceRetained};
  } finally {
    await context.close();
  }
}

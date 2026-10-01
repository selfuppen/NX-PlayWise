export const SUPPORTED_LANGS = ["zh", "en"];
export const DEFAULT_LANG = "zh";

export const TRANSLATIONS = {
  zh: {
    brand_eyebrow: "PlayWise",
    brand_name: "任我玩",
    badge_offline: "可离线使用",
    badge_standalone: "单文件离线版",
    lang_toggle_btn: "English",
    lang_toggle_aria: "切换语言 / Switch language",

    page_title: "生成今日加时码",
    slogan: "Play Wise. Play More.",
    intro_p_online: "加时码由当前设备的浏览器本地生成，本项目不会把生成输入提交给业务后端。",
    intro_p_standalone: "此单文件完全在当前浏览器中生成加时码，不需要 Python、服务器或网络连接。",
    download_standalone: "下载单文件离线版",
    install_button: "安装到设备",

    demo_warning_title: "当前使用公开演示密钥。",
    demo_warning_desc: "任何人都知道此密钥，且可生成多个加时码，最高可把当天额度累计到 1440 分钟。它只适用于已在 Switch 上主动开启演示模式的设备。",
    err_browser_capability: "当前浏览器缺少生成加时码所需的基础能力，请升级浏览器。",
    err_web_crypto: "当前页面无法使用 Web Crypto，请通过 HTTPS 或 localhost 打开。",
    err_selftest: "加时码算法自检失败，已停止生成：",
    warn_compat_subtle: "当前浏览器无法使用 Web Crypto，已启用内置 HMAC-SHA256 兼容实现；加时码协议和校验结果不变。",
    warn_compat_random: "当前浏览器无法使用安全随机源，将从当天尚未签发的编号中顺序选择；若 Switch 提示代码已使用，请重新生成。",
    warn_compat_storage: "当前打开方式不允许持久保存配置；关闭页面后需要重新导入，且旧代码发生编号碰撞时请重新生成。",

    pairing_title: "配对 Switch",
    pairing_p_online: "可扫描 Switch 二维码配对。配置文件不随安装包提供：先在 Switch 家长区打开“离线加时 → 手机/电脑生成”，按 A“导出配置文件”并验证 PIN；导出成功后，从 SD 卡 <code>/switch/playwise/parent-import.json</code> 复制到家长设备，再在此导入。",
    pairing_p_standalone: "配置文件不随安装包提供。先在 Switch 家长区打开“离线加时 → 手机/电脑生成”，按 A“导出配置文件”并验证 PIN；成功后从 SD 卡 <code>/switch/playwise/parent-import.json</code> 复制到家长设备，再在此导入。",
    import_file_button: "导入配置文件",

    pairing_dialog_title: "导入此设备？",
    pairing_dialog_device: "设备 ID",
    pairing_dialog_secret: "加时密钥",
    pairing_dialog_replace: "确认后将替换当前浏览器保存的设备配置，已有 nonce 历史会保留。",
    btn_cancel: "取消",
    btn_confirm_import: "导入此设备",
    pairing_source_qr: "检测到 Switch 二维码中的设备配置，请确认后导入。",
    pairing_source_file: "来自文件：{name}",
    err_import_size: "配置文件过大，预期为小于 16 KiB 的 parent-import.json",

    label_device: "Switch 设备 ID",
    label_secret: "加时密钥",
    secret_help: "会明文保存在当前浏览器中，请仅在可信的家长设备上使用。",
    btn_toggle_secret_show: "显示",
    btn_toggle_secret_hide: "隐藏",
    label_date: "生效日期（Switch 主机本地日期）",
    label_tier: "加时时长",
    btn_generate: "生成 8 位数字加时码",
    minutes_unit: "{m} 分钟",

    demo_dialog_title: "确认使用公开演示密钥",
    demo_dialog_p: "任何知道设备名的人都能生成多个有效加时码，并可把当天额度累计到 1440 分钟。请仅在 Switch 已主动启用演示模式时继续。",
    btn_understand_risk: "了解风险并继续",

    result_label: "请把下面的数字告诉孩子",
    result_meta: "{date} · {minutes} 分钟 · v2",
    btn_copy: "复制加时码",
    btn_copied: "已复制",
    err_copy_failed: "复制失败，请长按或选中加时码手动复制",
    collision_note: "若 Switch 提示“加时码已使用”，说明手机与电脑随机到了相同编号，请在本页重新生成一个。",

    settings_summary: "本机数据与安全说明",
    settings_p: "此页面会保存设备 ID、密钥、常用时长，以及当前浏览器当天已经签发的随机编号。清除浏览器站点数据也会删除这些内容。",
    btn_clear_config: "清除本机配置",

    troubleshooting_summary: "常见排错指南",
    faq_1_title: "无法配对或扫码后没有导入",
    faq_1_p: "确认二维码来自当前 Switch，并使用可信的 HTTPS 页面。自定义地址必须实现相同的 <code>#device_id</code> 与 <code>grant_secret</code> 配对参数；页面应在读取后立即清除地址栏参数。也可改用 <code>parent-import.json</code> 导入。",
    faq_2_title: "加时码无效或提示已使用",
    faq_2_p: "确认设备 ID、密钥、生效日期和 Switch 当前日期一致。修改密钥后，当前浏览器配对和使用原密钥签发的代码都会失效；提示已使用时请重新生成一个。",
    faq_3_title: "日期不正确",
    faq_3_p: "加时码按 Switch 主机的本地日期生效。请确认家长设备日期与 Switch 主机日期一致，再刷新状态并重新生成；不要继续尝试为错误日期生成的码。",
    faq_4_title: "设备没有响应",
    faq_4_p: "在 Switch 孩子区或家长区刷新状态，等待正在同步的操作完成。仍无响应时导出诊断信息，并检查 SD 卡中的 PlayWise 文件是否可读。",
    faq_5_title: "紧急停用已开启",
    faq_5_p: "紧急停用会阻止新的额度、周计划和加时码写入，但状态、诊断与恢复仍可使用。由家长在“支持与恢复”确认故障已排除后解除停用。",
    faq_6_title: "进一步诊断",
    faq_6_p: "诊断导出只包含白名单状态，不包含密钥、PIN、8 位码、完整 nonce 或恢复前像。高级排查可检查 <code>sdmc:/switch/playwise/logs/</code>、<code>flags/disable.flag</code> 和恢复状态；不要对外发送 <code>credentials.json</code>、<code>auth.json</code> 或 ledger。",

    footer_p_online: "正式部署请使用可信的 HTTPS 静态站点；电脑本机预览可使用 localhost。",
    footer_p_standalone: "这是可直接保存和打开的离线文件；移动浏览器若无法执行本地 HTML，请改用可信的 HTTPS 家长网页。",
    footer_nav_aria: "项目链接",
    footer_author: "作者 GitHub",
    footer_source: "项目源码",
  },
  en: {
    brand_eyebrow: "PlayWise",
    brand_name: "PlayWise",
    badge_offline: "Offline Ready",
    badge_standalone: "Standalone Offline",
    lang_toggle_btn: "中文",
    lang_toggle_aria: "Switch language / 切换语言",

    page_title: "Generate Daily Grant Code",
    slogan: "Play Wise. Play More.",
    intro_p_online: "Grant codes are generated locally in your browser. Inputs are never submitted to any backend server.",
    intro_p_standalone: "This standalone file generates grant codes entirely within your browser, requiring no Python, server, or internet connection.",
    download_standalone: "Download Standalone HTML",
    install_button: "Install App",

    demo_warning_title: "Public demo secret in use.",
    demo_warning_desc: "Anyone who knows this secret can generate valid grant codes, potentially accumulating up to 1440 minutes today. Use only on devices with Demo Mode explicitly enabled.",
    err_browser_capability: "Your browser lacks essential cryptographic features required to generate grant codes. Please upgrade your browser.",
    err_web_crypto: "Web Crypto is unavailable on this page. Please open via HTTPS or localhost.",
    err_selftest: "Grant code algorithm self-test failed; generation aborted: ",
    warn_compat_subtle: "Web Crypto is unavailable in this environment; using built-in HMAC-SHA256 fallback. Protocol and verification remain identical.",
    warn_compat_random: "Secure random generator is unavailable; sequential nonce selection active. If Switch reports code already used, generate a new one.",
    warn_compat_storage: "Persistent storage is not allowed in this environment. Configuration will not be saved across sessions.",

    pairing_title: "Pair Switch",
    pairing_p_online: "Scan the Switch QR code to pair. Config files are not bundled with installation packages: in Switch Parent Zone, navigate to \"Offline Grants → Phone/PC Generator\", press A to \"Export Config File\" and verify PIN; then copy <code>/switch/playwise/parent-import.json</code> from SD card to your device and import here.",
    pairing_p_standalone: "Config files are not bundled with installation packages. In Switch Parent Zone, open \"Offline Grants → Phone/PC Generator\", press A to \"Export Config File\" and verify PIN; then copy <code>/switch/playwise/parent-import.json</code> from SD card to your device and import here.",
    import_file_button: "Import Config File",

    pairing_dialog_title: "Import this device?",
    pairing_dialog_device: "Device ID",
    pairing_dialog_secret: "Grant Secret",
    pairing_dialog_replace: "Confirming will overwrite the currently saved device configuration. Existing nonce history will be preserved.",
    btn_cancel: "Cancel",
    btn_confirm_import: "Import This Device",
    pairing_source_qr: "Detected device configuration from Switch QR code. Please confirm to import.",
    pairing_source_file: "From file: {name}",
    err_import_size: "Config file too large. Expected parent-import.json under 16 KiB.",

    label_device: "Switch Device ID",
    label_secret: "Grant Secret",
    secret_help: "Saved in plaintext in this browser. Use only on trusted parental devices.",
    btn_toggle_secret_show: "Show",
    btn_toggle_secret_hide: "Hide",
    label_date: "Effective Date (Switch Local Date)",
    label_tier: "Grant Duration",
    btn_generate: "Generate 8-digit Grant Code",
    minutes_unit: "{m} min",

    demo_dialog_title: "Confirm Using Public Demo Secret",
    demo_dialog_p: "Anyone knowing this device ID can generate valid grant codes, potentially accumulating up to 1440 minutes today. Continue only if Demo Mode is explicitly enabled on your Switch.",
    btn_understand_risk: "Accept Risk & Continue",

    result_label: "Share this code with your child",
    result_meta: "{date} · {minutes} min · v2",
    btn_copy: "Copy Grant Code",
    btn_copied: "Copied!",
    err_copy_failed: "Copy failed. Please long-press or select the code to copy manually.",
    collision_note: "If the Switch indicates \"Grant code already used\", another device issued the same nonce. Simply generate a new code on this page.",

    settings_summary: "Local Data & Security",
    settings_p: "This page saves device ID, secret, selected duration, and nonces issued today. Clearing browser site data will remove this information.",
    btn_clear_config: "Clear Local Config",

    troubleshooting_summary: "Troubleshooting Guide",
    faq_1_title: "Pairing failed or QR code did not import",
    faq_1_p: "Ensure the QR code originates from your Switch and is opened via a trusted HTTPS page. Custom URLs must implement identical <code>#device_id</code> and <code>grant_secret</code> hash parameters; parameters should be cleared from the address bar immediately. Alternatively, import via <code>parent-import.json</code>.",
    faq_2_title: "Grant code invalid or already used",
    faq_2_p: "Verify that device ID, secret, effective date, and Switch system date match exactly. After changing the secret, existing pairings and codes signed with the old secret become invalid; generate a new one if marked as already used.",
    faq_3_title: "Incorrect date",
    faq_3_p: "Grant codes take effect based on the Switch console's local date. Confirm that your parental device date matches the Switch date, refresh status, and regenerate. Do not attempt to redeem codes generated for an incorrect date.",
    faq_4_title: "Console not responding",
    faq_4_p: "Refresh status in Switch Child or Parent Zone and wait for synchronization to finish. If still unresponsive, export diagnostic logs and verify that SD card files are readable.",
    faq_5_title: "Emergency disable active",
    faq_5_p: "Emergency disable prevents new playtime, schedule, and grant code writes, while status, diagnostics, and recovery remain accessible. Parents can deactivate emergency disable in \"Support & Recovery\" after resolving issues.",
    faq_6_title: "Further diagnostics",
    faq_6_p: "Diagnostic exports contain only allowlisted status info, excluding secrets, PINs, 8-digit codes, full nonces, or pre-install snapshots. For advanced troubleshooting, check <code>sdmc:/switch/playwise/logs/</code>, <code>flags/disable.flag</code>, and recovery states; never disclose <code>credentials.json</code>, <code>auth.json</code>, or ledger.",

    footer_p_online: "For production deployment, use a trusted HTTPS static site; for local desktop preview, localhost may be used.",
    footer_p_standalone: "This is a standalone offline file. If mobile browsers restrict local HTML execution, please use a trusted HTTPS parent web page.",
    footer_nav_aria: "Project Links",
    footer_author: "Author GitHub",
    footer_source: "Source Code",
  },
};

export function t(key, params = {}, lang = DEFAULT_LANG) {
  const dict = TRANSLATIONS[lang] || TRANSLATIONS[DEFAULT_LANG];
  let text = dict[key] || TRANSLATIONS[DEFAULT_LANG][key] || key;
  for (const [paramKey, paramVal] of Object.entries(params)) {
    text = text.replaceAll(`{${paramKey}}`, String(paramVal));
  }
  return text;
}

export function detectBrowserLanguage() {
  const navLang = (globalThis.navigator?.language || "").toLowerCase();
  if (navLang.startsWith("zh")) return "zh";
  if (navLang.startsWith("en")) return "en";
  return DEFAULT_LANG;
}

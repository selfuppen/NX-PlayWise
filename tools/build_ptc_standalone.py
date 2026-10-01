#!/usr/bin/env python3
from __future__ import annotations

import argparse
import base64
import hashlib
from pathlib import Path
import re
import sys


FRONTEND_ROOT = Path(__file__).with_name("ptc_frontend")
DEFAULT_OUTPUT = FRONTEND_ROOT / "playwise-offline.html"
IMPORT_PATTERN = re.compile(
    r'^import\s+\{.*?\}\s+from\s+["\'][^"\']+["\'];\s*',
    flags=re.MULTILINE | re.DOTALL,
)


def replace_once(value: str, old: str, new: str, label: str) -> str:
    if value.count(old) != 1:
        raise ValueError(f"standalone source expected exactly one {label}")
    return value.replace(old, new, 1)


def inline_javascript() -> str:
    parts: list[str] = []
    for filename in ["token.js", "pairing.js", "storage.js", "i18n.js", "app.js"]:
        source = (FRONTEND_ROOT / filename).read_text(encoding="utf-8")
        source = IMPORT_PATTERN.sub("", source)
        source = re.sub(r"^export\s+", "", source, flags=re.MULTILINE)
        if filename == "app.js":
            source = replace_once(
                source,
                '''  if (!standaloneMode && "serviceWorker" in navigator) {
    try {
      await navigator.serviceWorker.register("./sw.js", {scope: "./"});
      document.getElementById("offlineBadge").hidden = false;
    } catch (error) {
      console.warn("Service worker registration failed", error);
    }
  }
''',
                "",
                "service worker registration",
            )
        if re.search(r"^(?:import|export)\s", source, flags=re.MULTILINE):
            raise ValueError(f"unsupported module syntax remains in {filename}")
        parts.append(f"// {filename}\n{source.rstrip()}")
    return "\n\n".join(parts)


def csp_hash(value: str) -> str:
    digest = hashlib.sha256(value.encode("utf-8")).digest()
    return base64.b64encode(digest).decode("ascii")


def render_standalone() -> str:
    page = (FRONTEND_ROOT / "index.html").read_text(encoding="utf-8")
    css = (FRONTEND_ROOT / "styles.css").read_text(encoding="utf-8").rstrip()
    javascript = inline_javascript().rstrip()
    style_content = f"\n{css}\n"
    script_content = f"\n{javascript}\n"
    csp = (
        "default-src 'none'; "
        f"script-src 'sha256-{csp_hash(script_content)}'; "
        f"style-src 'sha256-{csp_hash(style_content)}'; "
        "img-src data:; connect-src 'none'; base-uri 'none'; form-action 'none'"
    )

    page = replace_once(page, '<html lang="zh-CN">', '<html lang="zh-CN" data-standalone="true">', "html root")
    page = replace_once(
        page,
        '<meta http-equiv="Content-Security-Policy" content="default-src \'self\'; script-src \'self\'; style-src \'self\'; img-src \'self\'; connect-src \'self\'; manifest-src \'self\'; worker-src \'self\'; base-uri \'none\'; form-action \'self\'; frame-ancestors \'none\'">',
        f'<meta http-equiv="Content-Security-Policy" content="{csp}">',
        "content security policy",
    )
    page = replace_once(page, '  <link rel="manifest" href="./manifest.webmanifest">\n', "", "manifest link")
    page = replace_once(page, '  <link rel="icon" href="./icon.svg" type="image/svg+xml">\n', "", "icon link")
    page = replace_once(page, '  <link rel="stylesheet" href="./styles.css">', f"  <style>{style_content}</style>", "stylesheet")
    page = replace_once(
        page,
        '          <a id="downloadStandalone" class="secondary compact" href="./playwise-offline.html" download data-i18n="download_standalone">下载单文件离线版</a>\n',
        "",
        "standalone download link",
    )
    page = replace_once(
        page,
        '<span id="offlineBadge" class="badge" hidden data-i18n="badge_offline">可离线使用</span>',
        '<span id="offlineBadge" class="badge" data-i18n="badge_standalone">单文件离线版</span>',
        "offline badge",
    )
    page = replace_once(
        page,
        "加时码由当前设备的浏览器本地生成，本项目不会把生成输入提交给业务后端。",
        "此单文件完全在当前浏览器中生成加时码，不需要 Python、服务器或网络连接。",
        "standalone introduction",
    )
    page = replace_once(
        page,
        "可扫描 Switch 二维码配对。配置文件不随安装包提供：先在 Switch 家长区打开“离线加时 → 手机/电脑生成”，按 A“导出配置文件”并验证 PIN；导出成功后，从 SD 卡 <code>/switch/playwise/parent-import.json</code> 复制到家长设备，再在此导入。",
        "配置文件不随安装包提供。先在 Switch 家长区打开“离线加时 → 手机/电脑生成”，按 A“导出配置文件”并验证 PIN；成功后从 SD 卡 <code>/switch/playwise/parent-import.json</code> 复制到家长设备，再在此导入。",
        "standalone pairing help",
    )
    page = replace_once(
        page,
        "正式部署请使用可信的 HTTPS 静态站点；电脑本机预览可使用 localhost。",
        "这是可直接保存和打开的离线文件；移动浏览器若无法执行本地 HTML，请改用可信的 HTTPS 家长网页。",
        "standalone footer",
    )
    page = replace_once(page, '  <script type="module" src="./app.js"></script>', f"  <script>{script_content}</script>", "module script")
    return page.rstrip() + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="Build the PlayWise single-file offline code generator.")
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="Fail when the output is missing or stale.")
    args = parser.parse_args()
    rendered = render_standalone()
    if args.check:
        try:
            current = args.output.read_text(encoding="utf-8")
        except OSError:
            print(f"standalone output is missing: {args.output}", file=sys.stderr)
            return 1
        if current != rendered:
            print(f"standalone output is stale: {args.output}", file=sys.stderr)
            return 1
        print(f"standalone output is current: {args.output}")
        return 0
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(rendered, encoding="utf-8", newline="\n")
    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

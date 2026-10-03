<div align="center">

  [English](DEVELOPMENT_ENVIRONMENT_GUIDE.md) | [简体中文](开发环境指南.md)

</div>

# Development Environment Guide

This document differentiates general project requirements, the maintainer's default environment, the devkitPro build environment, and physical Nintendo Switch hardware qualification. Paths, ports, and tool versions on individual machines are not project protocols; all developers must meet the same testing, packaging, and security quality gates.

## Environment Layers

| Layer | Required Components | Alternative or Optional Components |
| --- | --- | --- |
| Host Regression | Python 3, Git, deterministic fixtures | Windows, PowerShell, Docker, and real Switch are all optional |
| Host C Tests | C99 compiler, Make | Compiler and operating system can be substituted |
| Full Switch Build | devkitPro, devkitA64, libnx, FreeType, Make, and the repo unified packaging gate | SSH host/port, container name, and host OS can be substituted |
| Dependency Reproduction | Vendored libtesla and QR Code Generator in repo | `../libnx`, `../libtesla`, `../Atmosphere` are only for optional upstream research |
| Emulator Fast Iteration | devkitPro, and `build/eden-test/pctc-eden.nro` built via `make eden-test-nro` | Completely optional; used only for UI and business workflows, does not produce PCTL evidence |
| Hardware Qualification | Explicit console model, HOS, Atmosphère, and current candidate Zip | Host, container, and emulator tests cannot substitute |

## Host Quick Start

Platform-neutral entry point for local protocol, Python, frontend, and security packaging regression:

```text
python tools/test.py
```

These tests run against fixed timestamps, deterministic fixtures, and host adapters without accessing real Switch services. When modifying only Python code, protocol documents, or static frontend pages, use this entry point for rapid feedback.

`make test` additionally compiles host C tests, requiring a C99 compiler and Make. Any C, C++, Makefile, NRO, Overlay, sysmodule, or package modification must proceed to the unified devkitPro build verification.

## Eden Emulator Fast Iteration (Optional)

When iterating quickly on UI layouts and business workflows, run `make eden-test-nro` in the devkitPro environment to generate `build/eden-test/pctc-eden.nro`, and load this single NRO directly into Eden. It features an isolated `sdmc:/switch/playwise-eden` data root and an in-process simulated backend without needing a sysmodule or expanding packages. Full instructions, fixed test parameters, and constraints are documented in the [Testing Guide](TESTING_GUIDE.md). This is an optional iteration tool, does not belong to any release gate, and cannot serve as evidence for PCTL private commands or timing behavior.

## Full Switch Build

The authoritative, supported entry point for the project is:

```text
python tools/package_remote.py
```

This entry point connects via a single SSH session into the devkitPro environment to execute builds and verifications. To accelerate frequent development iterations, incremental compilation is enabled by default. It first executes lightweight host C/Python core unit tests and rendering primitive checks (skipping time-consuming full UI preview rendering and transcoding), then builds the three Switch release components, generates `release-manifest.json`, and packages/validates the standard installation package (`build/packages/playwise-<version>.zip`), while building and validating the Eden emulator test application (`build/eden-test/pctc-eden.nro`) by default.

Heavy and non-essential stages are decoupled from routine builds and can be triggered via dedicated flags:
- `--with-device-lab` (or `--device-lab`): Build and verify the Device Lab experimental sysmodule, NRO, Overlay, and Zip package.
- `--with-complete` (or `--complete`): Generate the standalone offline HTML frontend `playwise-offline.html` and package the user-facing `playwise-complete-<version>.zip` delivery bundle.
- `--with-previews`: Render console UI previews, convert to PNG, and synchronize to `docs/images/usage/` and `docs/images/usage-en/`.
- `--release`: One-step authoritative release mode, packaging standard, Eden, complete, and Device Lab zips alongside full preview sync.
- `--previews`: Only re-render and synchronize UI preview images (without compiling packages).
- `--skip-tests`: Skip container tests during active development for faster turnaround.

Testing and manifest generation run sequentially to avoid race conditions between test artifacts, cross-platform line ending conversions, and candidate source identity. When troubleshooting toolchain upgrades or external residue, use `python tools/package_remote.py --clean`; official release workflows explicitly run this clean build. The build environment requires accessible devkitPro/devkitA64, libnx, FreeType, Make, Git, Zip, and Python. The manifest captures the container image tag/digest, devkitA64 compiler, installed libnx package version, vendored libtesla commit, git commit, and tracked dirty status; release or Device Lab builds fail immediately if libnx cannot be resolved. Build status defaults to `qualification=pending`.

When primary features have been manually verified on real hardware, record a manual verification declaration directly via the unified packaging entry point:

```powershell
python tools/package_remote.py --clean --manual-device-verified
```

`--manual-device-verified` records `--verified-model "Nintendo Switch OLED" --verified-hos 22.5.0 --verified-atmosphere 1.11.2` by default; when verifying on different hardware or environments, explicitly override any parameter. These three environment values must correspond to the actual test console. In the standard package `build.json`, `qualification.status` and `verified_environment.result` are written as `manual_verified`, with `method=manual` and `scope=main_features`; Device Lab and Eden manifests remain `pending`. This declaration represents manual feature verification only, accurately preserves `source_dirty` in the manifest, and does not substitute for a full Device Lab qualification report. Once full qualification passes, verification and promotion tools can still be run against the original package.

The script requires the host and build container to share the exact same workspace: build artifacts are written into the mounted directory by the container and validated by the host. It is not a generic remote builder that pushes source code to an arbitrary machine and copies artifacts back. Avoid improvising candidate builds using multiple manual SSH commands, `docker exec`, or copying artifacts across machines.

## Maintainer Default Windows Environment

The current maintenance workflow runs on Windows, PowerShell, Docker Desktop, Windows OpenSSH, and Python 3. The default connection profile for `tools/package_remote.py` is:

| Parameter | Default Value |
| --- | --- |
| SSH host | `127.0.0.1` |
| SSH port | `1888` |
| SSH user | `root` |
| Container workspace | `/ws/playwise` |
| Local Docker container name | `devkitpro-ssh-v1` |

These values represent the maintainer's default configuration covered by scripts and tests, not a network topology mandatory for all contributors. An equivalent shared devkitPro environment can be targeted using these arguments:

```text
python tools/package_remote.py \
  --host <host> \
  --port <port> \
  --user <user> \
  --container-path <mounted-repository-path> \
  --identity <private-key-path> \
  --build-image <image-tag> \
  --build-image-digest <sha256:image-id>
```

In PowerShell, these arguments can be written on a single line. Container passwords may only be read interactively by OpenSSH; automated connections should specify an authorized private key via `--identity`. The default local profile automatically queries the image tag and immutable image ID via `docker inspect devkitpro-ssh-v1` and exports them to the SSH build command; specify custom container names with `--docker-container`, or supply explicit identities for non-local Docker/remote environments via `--build-image` and `--build-image-digest`. Host environment variables `PLAYWISE_BUILD_IMAGE` and `PLAYWISE_BUILD_IMAGE_DIGEST` also serve as explicit overrides. If autodetection fails, the script warns and leaves the manifest as `unknown`, preventing entry into official release qualification.

The repository's `docker/docker-compose.yml` provides a local maintainer example. Before first use, modify the host volume mount path to point to your repository's parent folder, ensuring the in-container path matches `--container-path`. Compose maps host port `1888` to container SSH port `58791` by default; for local use, bind only to loopback via `127.0.0.1:1888:58791`.

The example Compose configuration uses a named Docker volume `playwise-devkitpro-ssh-keys` to persist container `/root/.ssh`. After appending public keys to `authorized_keys` once, rebuilding images or executing `docker compose up -d --force-recreate` retains key authorization; `docker compose down -v` or manually removing the volume clears authorization.

The development image permits root SSH and defines a development password in the Dockerfile; therefore, ports must strictly bind to trusted loopback development environments and never be exposed to local area networks or the public internet. Prefer authorized SSH keys and restrict host firewall rules. The image is based on the devkitA64 `20260215` tag; this is a fixed tag, not a pinned digest.

## Dependencies and Upstream Source Trees

Actual builds link against:

- libnx from the devkitPro environment;
- vendored libtesla in `companion/overlay/vendor/libtesla/`;
- vendored QR Code Generator in `third_party/qrcodegen/`.

The maintainer workstation provides three optional, read-only upstream checkouts in adjacent directories: `../libnx` for verifying services, dispatching, and public APIs; `../libtesla` for verifying Overlay lifecycle, rendering, and input handling; and `../Atmosphere` for verifying CFW boot2/content flags, SM/PM, hbloader configs, crash reporting, and version compatibility. Other developers do not need these checkouts; they do not participate in PlayWise compilation, packaging, or dependency reproduction. Actual builds strictly use devkitPro's libnx and repository vendored dependencies. When synchronizing vendored dependencies, always update the repository, commit/tag, license, and check date in the corresponding `UPSTREAM.txt`.

Before drawing conclusions from local Atmosphère source code, record the exact commit, human-readable version, and working tree status:

```text
git -C ../Atmosphere rev-parse HEAD
git -C ../Atmosphere describe --tags --always --dirty
git -C ../Atmosphere status --short
```

Conclusions must cite tracked source code under that commit; untracked files may simply be local notes or temporary artifacts and cannot serve as upstream evidence. The Atmosphère checkout explains CFW boot and runtime environment context, but is not an alternative source for Horizon private PCTL protocol, libnx APIs, or libtesla APIs. Reading criteria are detailed in the [Developer Guide](DEVELOPER_GUIDE.md), and PCTL evidence boundaries are documented in [PCTL Integration Architecture](PCTL_ARCHITECTURE.md).

Upstream research order is: repository documentation and protocol specs, vendored pinned versions, available local upstream checkouts, and finally GitHub. Local checkouts must also bind to recorded commits/tags; remote `main` or `master` branches cannot replace pinned versions or explicit version evidence. Remote queries must comply with repository upstream research rules; finding a newer upstream release does not justify unrequested dependency or qualification baseline upgrades.

## CI & Deployment Status

Currently, GitHub Actions only builds and deploys the Parent Web App (PWA) static assets. Switch C/C++, NRO, Overlay, sysmodule, and package builds are not yet run in CI; authoritative gates are executed in the maintainer's devkitPro environment. A successful Pages workflow run must never be equated to candidate Switch package verification.

## Real Device Verification

Hardware qualification divides into at least two categories:

- UI & Interaction Verification: covering Companion NRO, Overlay, touch, controller input, status refresh, grant code preview/confirmation, and interrupt recovery;
- Release Qualification: using the candidate release Zip on explicitly recorded combinations of console model, HOS, and Atmosphère, following the complete workflow in the [Testing Guide](TESTING_GUIDE.md); binding the report to the original Zip SHA-256 via `verify_device_qualification.py`, and promoting the byte-identical package to `build/qualified/` using `promote_qualified_build.py`. Candidates may start as `pending` or be marked as `manual_verified`; full qualification still requires zero tracked dirty modifications and matching report identities. Recompiling invalidates prior qualification verdicts.

PCTL private command parameter units, 0x44 layouts, and timer behavior are established exclusively through repository protocol specifications, deterministic vectors, and hardware A/B evidence, never inferred from the absence of public libnx wrappers. Emulators (including Eden) employ simulated PCTL adapters defined entirely by repository mock logic, and therefore do not constitute qualification evidence of any kind.

## Platform Considerations

- Chinese documentation in the repository uses UTF-8. Under PowerShell 5.1, read files explicitly with `Get-Content <file> -Encoding utf8`.
- When creating Chinese Git commit messages on Windows, write a BOM-free UTF-8 text file and verify the title with `git log -1 --pretty=%B`.
- Linux and macOS can run platform-neutral Python host regressions directly; full Switch builds still require connecting to a devkitPro environment matching the contracts above.
- If build artifacts do not appear in the host workspace, verify that shared volume mounts and `--container-path` point to the exact same repository path, then inspect SSH host keys, ports, users, and private key permissions.

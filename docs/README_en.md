# Documentation Index

[English](README_en.md) | [简体中文](README.md) | [Project home](../README_en.md)

Documents are grouped by purpose. Chinese and English versions share each category directory; screenshots remain under `images/`.

## Where to start

- Installation and daily use: read the [User Guide](使用/USER_GUIDE.md), then the [calendar format](使用/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md) when creating a region calendar.
- Development and validation: start with the [Development Environment Guide](开发/DEVELOPMENT_ENVIRONMENT_GUIDE.md), followed by the [Developer Guide](开发/DEVELOPER_GUIDE.md) and [Testing Guide](开发/TESTING_GUIDE.md).
- Protocol and control behavior: consult the [Protocol Specification](设计/PROTOCOL.md) and [PCTL Integration Architecture](设计/PCTL_ARCHITECTURE.md). UI styling and palettes follow the [Visual Style Guide](设计/VISUAL_STYLE_GUIDE.md). Built-in calendar sources are in [Holiday Calendar Data](设计/节假日日历.md) (Chinese).
- Exploring capabilities: consult the research records below. Findings and proposed experiments do not establish released features or hardware qualification for the current candidate.

## Usage — `使用/`

| Document | Contents | 简体中文 |
| --- | --- | --- |
| [User Guide](使用/USER_GUIDE.md) | Time control logic, installation, setup, plans, grants, overlay, troubleshooting and upgrades | [使用指南](使用/使用指南.md) |
| [Custom Holiday Calendar Format](使用/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md) | Import fields, complete JSON and invalid examples | [自制节假日日历格式](使用/自制节假日日历格式.md) |

## Development — `开发/`

| Document | Contents | 简体中文 |
| --- | --- | --- |
| [Development Environment Guide](开发/DEVELOPMENT_ENVIRONMENT_GUIDE.md) | Host, devkitPro, unified builds and dependencies | [开发环境指南](开发/开发环境指南.md) |
| [Developer Guide](开发/DEVELOPER_GUIDE.md) | Module responsibilities, safety boundaries, upstream research and merge requirements | [开发指南](开发/开发指南.md) |
| [Testing Guide](开发/TESTING_GUIDE.md) | Local regression, CI gate, packaging gates, Eden, UI, hardware acceptance and evidence round-trip | [测试指南](开发/测试指南.md) |
| [Unattended Updates and Tests (Chinese)](开发/无人值守更新与测试指南.md) | Bounded deployment, PIN authorization, recovery, evidence and test readiness | [无人值守更新与测试指南](开发/无人值守更新与测试指南.md) |
| [PCTL1952 Candidate Acceptance (Chinese)](开发/PCTL1952候选验收.md) | Direct v2 and legacy proxy v1 contracts, raw pairing and qualification gates | [PCTL1952候选验收](开发/PCTL1952候选验收.md) |
| [Reusable Test Procedure (Chinese)](开发/测试操作手册.md) | Bounded batches, one-time requests, recovery, offline reruns and grouped observations | [测试操作手册](开发/测试操作手册.md) |
| [Stage and Acceptance Records (Chinese)](开发/验收记录/README.md) | Monthly archives preserving historical candidate and failure boundaries | [验收记录](开发/验收记录/README.md) |
| [Tool Categories (Chinese)](../tools/README.md) | Daily entry points, dependencies, specialized and historical tools, and disposable caches | [目录整理说明](开发/目录整理说明.md) |

## Stage-specific material

Consult these only for the relevant task; they are not global development or testing prerequisites. The [October 2026 progress summary](开发/当前进度与后续指导.md) covers the 1952 migration, removal of the 1440-minute proxy and associated hardware validation. [Acceptance records](开发/验收记录/README.md) retain each batch's facts and scope.

## Design — `设计/`

| Document | Contents | 简体中文 |
| --- | --- | --- |
| [Protocol Specification](设计/PROTOCOL.md) | Threat model and trust boundaries, files, requests, results, errors and recovery contracts | [协议](设计/协议.md) |
| [PCTL Integration Architecture](设计/PCTL_ARCHITECTURE.md) | Handover, write transactions, private-command evidence and Device Lab boundaries | [PCTL集成架构](设计/PCTL集成架构.md) |
| [Visual Style Guide](设计/VISUAL_STYLE_GUIDE.md) | Palette, semantic colors, edge-to-edge layout and software rendering rules | [视觉风格与设计指南](设计/视觉风格与设计指南.md) |
| Holiday Calendar Data (Chinese only) | Built-in data versions, dates, official sources and fallback behavior | [节假日日历数据](设计/节假日日历.md) |

## Research — `调研/`

For device experiments, see [Stable device probing workflow (Chinese)](开发/真机稳定探测方法.md): bounded same-process capture, transaction recovery, unattended evidence and qualified human observations.

Use the [research index](调研/README.md) (Chinese) to separate reusable references, topic analysis and historical investigations. Current operations belong in the development guides.

| Record (Chinese only) | Scope | Date |
| --- | --- | --- |
| [Enabling system Parental Controls](调研/开启系统家长控制功能调研.md) | Native PIN registration, handover prerequisites and proposed entry point | 2026-10-06 |
| [Non-PCTL suspension and overlay capabilities](调研/非PCTL暂停与浮窗能力调研.md) | Source-level display, input, process suspension and recovery capabilities | 2026-10-06 |
| [Overlay memory and freeze risk audit](调研/Overlay内存与死机风险自查.md) | Bounded glyph ownership, resource teardown, thread races and hardware evidence limits | 2026-10-13 |
| [PCTL1952 feature impact analysis](调研/专题分析/PCTL1952功能影响分析.md) | Cross-feature analysis bound to its original source baseline, not the current backlog | 2026-10-14 |
| [Historical investigations](调研/历史调查/README.md) | Five diagnostic, capability-probe and handoff records with original version and evidence boundaries | 2026-10 |

## Maintenance

Place new documents in the appropriate category and update both indexes. Keep translations together, link between them, and use relative paths across categories. Commands and source paths measured from the repository root retain their original meaning.

Research records retain dates, source versions and validation limits. Maintain released behavior and stable contracts in the usage, development and design documents. Hardware acceptance records added to the repository belong under `开发/验收记录/` and should be linked from the Testing Guide; research findings do not substitute acceptance evidence.

Keep `images/usage/` and `images/usage-en/` at their existing paths. See [Documentation & Screenshot Maintenance](使用/USER_GUIDE.md#documentation--screenshot-maintenance) for preview synchronization. The two legacy calendar-format paths in this directory provide navigation for help URLs in released applications; maintain their content only under `使用/`.

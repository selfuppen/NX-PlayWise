# Documentation Index

[English](README_en.md) | [简体中文](README.md) | [Project home](../README_en.md)

Documents are grouped by purpose. Chinese and English versions share each category directory; screenshots remain under `images/`.

## Where to start

- Installation and daily use: read the [User Guide](使用/USER_GUIDE.md), then the [calendar format](使用/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md) when creating a region calendar.
- Development and validation: start with the [Development Environment Guide](开发/DEVELOPMENT_ENVIRONMENT_GUIDE.md), followed by the [Developer Guide](开发/DEVELOPER_GUIDE.md) and [Testing Guide](开发/TESTING_GUIDE.md).
- Protocol and control behavior: consult the [Protocol Specification](设计/PROTOCOL.md) and [PCTL Integration Architecture](设计/PCTL_ARCHITECTURE.md). Built-in calendar sources are in [Holiday Calendar Data](设计/节假日日历.md) (Chinese).
- Exploring capabilities: consult the research records below. Findings and proposed experiments do not establish released features or hardware qualification for the current candidate.

## Usage — `使用/`

| Document | Contents | 简体中文 |
| --- | --- | --- |
| [User Guide](使用/USER_GUIDE.md) | Installation, setup, plans, grants, overlay, troubleshooting and upgrades | [使用指南](使用/使用指南.md) |
| [Custom Holiday Calendar Format](使用/CUSTOM_HOLIDAY_CALENDAR_FORMAT.md) | Import fields, complete JSON and invalid examples | [自制节假日日历格式](使用/自制节假日日历格式.md) |

## Development — `开发/`

| Document | Contents | 简体中文 |
| --- | --- | --- |
| [Development Environment Guide](开发/DEVELOPMENT_ENVIRONMENT_GUIDE.md) | Host, devkitPro, unified builds and dependencies | [开发环境指南](开发/开发环境指南.md) |
| [Developer Guide](开发/DEVELOPER_GUIDE.md) | Module responsibilities, safety boundaries, upstream research and merge requirements | [开发指南](开发/开发指南.md) |
| [Testing Guide](开发/TESTING_GUIDE.md) | Local regression, packaging gates, Eden, UI and hardware acceptance | [测试指南](开发/测试指南.md) |

## Design — `设计/`

| Document | Contents | 简体中文 |
| --- | --- | --- |
| [Protocol Specification](设计/PROTOCOL.md) | Files, requests, results, errors and recovery contracts | [协议](设计/协议.md) |
| [PCTL Integration Architecture](设计/PCTL_ARCHITECTURE.md) | Handover, write transactions, private-command evidence and Device Lab boundaries | [PCTL集成架构](设计/PCTL集成架构.md) |
| Holiday Calendar Data (Chinese only) | Built-in data versions, dates, official sources and fallback behavior | [节假日日历数据](设计/节假日日历.md) |

## Research — `调研/`

| Record (Chinese only) | Scope | Date |
| --- | --- | --- |
| [Enabling system Parental Controls](调研/开启系统家长控制功能调研.md) | Native PIN registration, handover prerequisites and proposed entry point | 2026-10-06 |
| [Non-PCTL suspension and overlay capabilities](调研/非PCTL暂停与浮窗能力调研.md) | Source-level display, input, process suspension and recovery capabilities | 2026-10-06 |
| [Bedtime timer activation diagnosis](调研/就寝恢复误启动计时诊断-2026-10-08.md) | SD evidence, version comparison, recovery boundaries and device retest | 2026-10-08 |

## Maintenance

Place new documents in the appropriate category and update both indexes. Keep translations together, link between them, and use relative paths across categories. Commands and source paths measured from the repository root retain their original meaning.

Research records retain dates, source versions and validation limits. Maintain released behavior and stable contracts in the usage, development and design documents. Hardware acceptance records added to the repository belong under `开发/验收记录/` and should be linked from the Testing Guide; research findings do not substitute acceptance evidence.

Keep `images/usage/` and `images/usage-en/` at their existing paths. See [Documentation & Screenshot Maintenance](使用/USER_GUIDE.md#documentation--screenshot-maintenance) for preview synchronization. The two legacy calendar-format paths in this directory provide navigation for help URLs in released applications; maintain their content only under `使用/`.

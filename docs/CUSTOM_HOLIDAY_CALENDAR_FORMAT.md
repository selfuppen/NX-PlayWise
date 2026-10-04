# Custom region holiday calendar format

Create one UTF-8 JSON file per region and year. Save it in SD `/switch/playwise/calendar-import/`, then open Parent Zone → Time Plans → Holiday Schedule → Manage Calendars → Import Files. Preview the file and confirm import. Import updates the library only: select the region in Saved Regions and hold to apply its imported years. Re-importing an active year also requires applying again. Built-in mainland China remains a separate option. Uncovered years fall back to the weekly plan. Imported content is labeled User Import; PlayWise does not verify its official source.

## Controls and on-screen help

`B` returns, `Y` refreshes files, saved regions and backend status, and `X` opens brief file-format help. All displayed buttons support touch. Use `L/R` to switch lists, Up/Down to select, Left/Right or `ZL/ZR` to page, and `A/+` to open confirmation. Close help with `A`, `B`, `+` or its touch back button; the selected list, page and item remain. Back and help remain available during refresh; repeated refresh, import and apply are blocked.

## Fields

| Field | Requirement |
| --- | --- |
| `format_version` | Integer `1`; this is a format version, not a content revision. |
| `region_id` | 2–16 uppercase letters, digits or internal hyphens, such as `JP` or `US-CA`. |
| `region_name` | Nonempty UTF-8 display name, at most 64 bytes. Use the same name for every year with the same ID. |
| `year` | Integer from 2020 to 2198. One file covers this year only. |
| `holidays` | Required array, possibly empty. Each entry contains nonempty `name`, `from` and `to`; both dates are inclusive. |
| `workdays` | Required array of date strings, possibly empty, for special working days. |
| `source_note` | Optional nonempty source description, at most 128 bytes. This does not certify official status. |

Dates must be real dates in `YYYY-MM-DD` format belonging to `year`. Holiday start must not follow end. Holiday ranges, working dates and their intersections must not overlap or duplicate. At most 64 holiday groups are allowed; file size must not exceed 16 KiB. Filenames consist of 1–75 letters, digits, `-`, `_` or `.` followed by `.json`, without separators or `..`. No quota, bedtime or timezone fields belong in the file. Dates refer to full local days on the Switch; configure quotas and bedtime in Time Plans.

## Complete example

Save as `JP-2028.json` in UTF-8. These dates demonstrate the format only; check the actual local schedule yourself.

```json
{
  "format_version": 1,
  "region_id": "JP",
  "region_name": "Japan (custom example)",
  "year": 2028,
  "holidays": [
    {"name": "Example New Year", "from": "2028-01-01", "to": "2028-01-03"},
    {"name": "Example spring holiday", "from": "2028-03-20", "to": "2028-03-20"}
  ],
  "workdays": ["2028-01-08"],
  "source_note": "Family test data; verify local published dates"
}
```

2028 is a leap year, so `2028-02-29` is valid. With no holidays or working days, keep `"holidays": []` and `"workdays": []`.

## Invalid example

The following file is rejected: 2027 has no February 29, and May 2 conflicts with the holiday range.

```json
{
  "format_version": 1,
  "region_id": "US-CA",
  "region_name": "California",
  "year": 2027,
  "holidays": [
    {"name": "Invalid date", "from": "2027-02-29", "to": "2027-02-29"},
    {"name": "Overlap", "from": "2027-05-01", "to": "2027-05-03"}
  ],
  "workdays": ["2027-05-02"]
}
```

Edit and re-import a region/year without increasing `format_version`. The library retains digest-named files and updates its index; the active version stays pinned until applied again. If a source changes, its digest differs or a file is damaged, check the SD file and refresh the preview before confirming.

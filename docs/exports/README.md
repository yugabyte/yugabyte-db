# Docs exports (CSV for spreadsheets)

## `stable-yb-master-flags-for-google-sheet.csv`

Tabular export of **stable** [`yb-master` configuration reference](../content/stable/reference/configuration/yb-master.md): one row per `#####` flag block (duplicate headings appear on multiple rows with `occurrence_index` &gt; 1).

**Import into Google Sheets**

1. Google Drive → **New** → **Google Sheets** → **Blank spreadsheet**.
2. **File** → **Import** → **Upload** (or drag the CSV onto the sheet).
3. Choose **Replace spreadsheet** or **Insert new sheet**, separator **Comma**, character set **UTF-8**.

**Columns**

| Column | Meaning |
|--------|---------|
| `occurrence_index` | 1st, 2nd, … occurrence when the same flag appears twice on the page |
| `section` | Nearest preceding `##` heading |
| `flag_heading` | Raw markdown heading text after `#####` |
| `flag_name_cli` | Approximate CLI-style name (best-effort) |
| `restart_needed_badge` | `yes` if `{{<tags/feature/restart-needed>}}` is in the block |
| `master_tserver_match_badge` | `yes` if `{{<tags/feature/t-server>}}` is in the block |
| `other_badges_or_notes` | EA / TP / deprecated hints from shortcodes or heading |
| `default_documented` | Last `Default:` line found in the block (may be incomplete for complex blocks) |
| `required_documented` | `yes` if the block contains a standalone `Required.` line |
| `description_excerpt` | Concatenated prose from the block (links stripped; truncated) |
| `source_path` | Repo-relative path to the markdown file |

Regenerate after editing `yb-master.md`:

```bash
cd yugabyte-db/docs/exports
python3 export_yb_master_flags_csv.py
# Or another tree:
python3 export_yb_master_flags_csv.py ../content/v2025.1/reference/configuration/yb-master.md -o v2025.1-yb-master-flags.csv
```

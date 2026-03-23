#!/usr/bin/env python3
"""Export yb-master.md flag sections to CSV for Google Sheets."""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


def _source_path_for_sheet(md_path: Path) -> str:
    parts = md_path.resolve().parts
    if "docs" in parts:
        i = parts.index("docs")
        return "/".join(parts[i:])
    return str(md_path)


def export_md_to_csv(md_path: Path, out_path: Path) -> int:
    text = md_path.read_text(encoding="utf-8")
    if text.startswith("---"):
        end = text.index("---", 3) + 3
        text = text[end:].lstrip("\n")

    lines = text.splitlines()
    current_section = ""
    rows: list[dict[str, str]] = []
    occurrence: dict[str, int] = {}

    i = 0
    while i < len(lines):
        line = lines[i]
        sec_m = re.match(r"^## (.+)$", line)
        if sec_m and not line.startswith("###"):
            current_section = sec_m.group(1).strip()
            i += 1
            continue
        flag_m = re.match(r"^##### (.+)$", line)
        if flag_m:
            heading = flag_m.group(1).strip()
            key = re.sub(r"^--", "", heading).split()[0].lower()
            occurrence[key] = occurrence.get(key, 0) + 1
            occ_n = occurrence[key]

            body: list[str] = []
            i += 1
            while i < len(lines):
                L = lines[i]
                if re.match(r"^##### ", L) or (
                    re.match(r"^## ", L) and not L.startswith("###")
                ):
                    break
                body.append(L)
                i += 1

            body_text = "\n".join(body)
            restart = "yes" if "tags/feature/restart-needed" in body_text else "no"
            match_mt = "yes" if "tags/feature/t-server" in body_text else "no"
            other: list[str] = []
            if "tags/feature/ea" in body_text or "{{<tags/feature/ea" in body_text:
                other.append("EA")
            if "tags/feature/tp" in body_text or "tags/feature/tech-preview" in body_text:
                other.append("TP")
            if "deprecated" in heading.lower():
                other.append("deprecated heading")
            if "tags/feature/deprecated" in body_text:
                other.append("deprecated badge")

            default_val = ""
            for dm in re.finditer(r"Default:\s*(.+)$", body_text, re.MULTILINE):
                default_val = dm.group(1).strip()
                default_val = re.sub(r"^`([^`]*)`$", r"\1", default_val)
            required = (
                "yes" if re.search(r"^Required\.\s*$", body_text, re.MULTILINE) else "no"
            )

            desc_lines: list[str] = []
            skip_block = False
            for L in body:
                s = L.strip()
                if not s:
                    continue
                if s.startswith("{{") or s.startswith("{{%"):
                    continue
                if s.startswith("```"):
                    skip_block = not skip_block
                    continue
                if skip_block:
                    continue
                if s.startswith("|") and "---" not in s:
                    continue
                if s.startswith("#"):
                    continue
                desc_lines.append(s)
                if sum(len(x) for x in desc_lines) > 800:
                    break
            description = " ".join(desc_lines)
            description = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", description)
            description = description.replace("`", "'")[:1200]

            if heading.startswith("--"):
                flag_cli = heading
            elif heading.startswith("ysql") or heading.startswith("enforce"):
                flag_cli = heading.split()[0]
            else:
                flag_cli = heading

            rows.append(
                {
                    "occurrence_index": str(occ_n),
                    "section": current_section,
                    "flag_heading": heading,
                    "flag_name_cli": flag_cli,
                    "restart_needed_badge": restart,
                    "master_tserver_match_badge": match_mt,
                    "other_badges_or_notes": "; ".join(other) if other else "",
                    "default_documented": (default_val[:500] if default_val else ""),
                    "required_documented": required,
                    "description_excerpt": description,
                    "source_path": _source_path_for_sheet(md_path),
                }
            )
            continue
        i += 1

    out_path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = list(rows[0].keys()) if rows else []
    with out_path.open("w", newline="", encoding="utf-8-sig") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames, quoting=csv.QUOTE_MINIMAL)
        w.writeheader()
        w.writerows(rows)
    return len(rows)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "md_path",
        type=Path,
        nargs="?",
        default=Path(__file__).resolve().parents[1]
        / "content/stable/reference/configuration/yb-master.md",
        help="Path to yb-master.md",
    )
    p.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "stable-yb-master-flags-for-google-sheet.csv",
        help="Output CSV path",
    )
    args = p.parse_args()
    n = export_md_to_csv(args.md_path, args.output)
    print(f"Wrote {n} rows to {args.output}")


if __name__ == "__main__":
    main()

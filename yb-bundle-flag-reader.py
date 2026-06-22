#!/usr/bin/env python3
"""
Extract and compare gflag settings from a YugabyteDB support bundle.

Reads master/conf/server.conf and tserver/conf/server.conf for every node
in a support bundle and displays them in a consolidated table.  Works on
both .tar.gz bundles and already-extracted directories.

Requires only the Python 3 standard library.
"""

import argparse
import csv
import io
import os
import re
import shutil
import sys
import tarfile
from collections import defaultdict
from typing import Dict, List, Optional, Set, Tuple

# {node_name: {flag_name: value}}
FlagMap = Dict[str, Dict[str, str]]

CONF_RE = re.compile(r"(?:^|/)(?P<process>master|tserver)/conf/server\.conf$")
FLAG_LINE_RE = re.compile(r"^--([^=]+)=(.*)$")


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------


def parse_server_conf(text: str) -> Dict[str, str]:
    """Parse ``--key=value`` lines from a server.conf file."""
    flags: Dict[str, str] = {}
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        m = FLAG_LINE_RE.match(line)
        if m:
            flags[m.group(1)] = m.group(2)
    return flags


def _node_name_from_path(path: str) -> Tuple[Optional[str], Optional[str]]:
    """Return (node_name, process_type) from a path containing .../node/master|tserver/conf/server.conf."""
    parts = [p for p in path.replace("\\", "/").split("/") if p]
    for i, part in enumerate(parts):
        if part in ("master", "tserver") and i > 0:
            candidate = parts[i - 1]
            if not candidate.startswith("yb-support-bundle"):
                return candidate, part
    return None, None


# ---------------------------------------------------------------------------
# Bundle scanning
# ---------------------------------------------------------------------------


def scan_directory(dir_path: str) -> Tuple[FlagMap, FlagMap]:
    master_flags: FlagMap = {}
    tserver_flags: FlagMap = {}
    for root, _dirs, files in os.walk(dir_path):
        for fname in files:
            if fname != "server.conf":
                continue
            fpath = os.path.join(root, fname)
            rel = os.path.relpath(fpath, dir_path)
            node, process = _node_name_from_path(rel)
            if not node or not process:
                continue
            try:
                with open(fpath, "r", encoding="utf-8", errors="replace") as f:
                    flags = parse_server_conf(f.read())
            except OSError:
                continue
            if process == "master":
                master_flags[node] = flags
            else:
                tserver_flags[node] = flags
    return master_flags, tserver_flags


def scan_tar(tar_path: str) -> Tuple[FlagMap, FlagMap]:
    master_flags: FlagMap = {}
    tserver_flags: FlagMap = {}

    with tarfile.open(tar_path, "r:*") as tf:
        for member in tf.getmembers():
            if not member.isfile():
                continue
            if not member.name.endswith("server.conf"):
                continue
            node, process = _node_name_from_path(member.name)
            if not node or not process:
                # Also try nested component tars? Usually already extracted.
                continue
            try:
                f = tf.extractfile(member)
                if not f:
                    continue
                flags = parse_server_conf(f.read().decode("utf-8", errors="replace"))
            except Exception:
                continue
            if process == "master":
                master_flags[node] = flags
            else:
                tserver_flags[node] = flags

    return master_flags, tserver_flags


# ---------------------------------------------------------------------------
# ANSI helpers
# ---------------------------------------------------------------------------

_USE_COLOR = True


def _color_enabled() -> bool:
    return _USE_COLOR and hasattr(sys.stdout, "isatty") and sys.stdout.isatty()


def _ansi(code: str, text: str) -> str:
    if not _color_enabled():
        return text
    return f"\033[{code}m{text}\033[0m"


def _bold(t: str) -> str:
    return _ansi("1", t)


def _dim(t: str) -> str:
    return _ansi("2", t)


def _fg_cyan(t: str) -> str:
    return _ansi("36", t)


def _fg_yellow(t: str) -> str:
    return _ansi("33", t)


def _fg_red(t: str) -> str:
    return _ansi("31", t)


def _fg_green(t: str) -> str:
    return _ansi("32", t)


def _bold_cyan(t: str) -> str:
    return _ansi("1;36", t)


def _bold_yellow(t: str) -> str:
    return _ansi("1;33", t)


# ---------------------------------------------------------------------------
# Display helpers
# ---------------------------------------------------------------------------

# Box-drawing pieces
_H = "─"
_V = "│"
_TL = "┌"
_TR = "┐"
_BL = "└"
_BR = "┘"
_TJ = "┬"
_BJ = "┴"
_LJ = "├"
_RJ = "┤"
_CJ = "┼"


def _hline(widths: List[int], left: str, mid: str, right: str) -> str:
    return left + mid.join(_H * (w + 2) for w in widths) + right


_ANSI_RE = re.compile(r"\033\[[0-9;]*m")


def _visible_len(s: str) -> int:
    """Length of *s* ignoring ANSI escape sequences."""
    return len(_ANSI_RE.sub("", s))


def _pad(s: str, width: int) -> str:
    """Left-align *s* to *width* visible characters, respecting ANSI escapes."""
    gap = width - _visible_len(s)
    return s + " " * max(gap, 0)


def _row(cells: List[str], widths: List[int]) -> str:
    padded = [f" {_pad(c, w)} " for c, w in zip(cells, widths)]
    return _V + _V.join(padded) + _V


def _truncate(val: str, width: int) -> str:
    if len(val) <= width:
        return val
    return val[: width - 1] + "…"


def _all_flag_names(flag_map: FlagMap) -> List[str]:
    names: Set[str] = set()
    for flags in flag_map.values():
        names.update(flags.keys())
    return sorted(names)


def _is_uniform(flag_name: str, flag_map: FlagMap) -> bool:
    """True when every node that has this flag has the same value."""
    vals = {flags[flag_name] for flags in flag_map.values() if flag_name in flags}
    return len(vals) <= 1


def _term_width() -> int:
    try:
        return shutil.get_terminal_size((120, 40)).columns
    except Exception:
        return 120


def _auto_value_width(flag_col_w: int, n_nodes: int) -> int:
    """Pick per-node column width that fits the terminal, with a sane floor."""
    tw = _term_width()
    # Budget: flag_col + n_nodes * (val_col + 3 border chars) + 1 trailing border
    available = tw - flag_col_w - 3 - 1
    if n_nodes > 0:
        per_node = (available // n_nodes) - 3
    else:
        per_node = 40
    return max(per_node, 16)


def print_flag_table(
    process_type: str,
    flag_map: FlagMap,
    diff_only: bool = False,
    value_width: int = 0,
):
    if not flag_map:
        print(f"\n  {_dim('No')} {process_type} {_dim('server.conf files found.')}\n")
        return

    nodes = sorted(flag_map.keys())
    all_flags = _all_flag_names(flag_map)
    if not all_flags:
        print(f"\n  {_dim('No flags found in')} {process_type} {_dim('server.conf files.')}\n")
        return

    if diff_only:
        all_flags = [f for f in all_flags if not _is_uniform(f, flag_map)]
        if not all_flags:
            print(
                f"\n  {_fg_green('✓')} All {_bold(process_type)} flags are "
                f"identical across {len(nodes)} node(s).\n"
            )
            return

    flag_col_w = max((len(f) for f in all_flags), default=4)
    flag_col_w = max(flag_col_w, 4)

    if value_width > 0:
        node_col_w = value_width
    else:
        node_col_w = _auto_value_width(flag_col_w, len(nodes))
    node_col_w = max(node_col_w, max(len(n) for n in nodes))

    widths = [flag_col_w] + [node_col_w] * len(nodes)

    # Title banner
    label = process_type.upper()
    subtitle = "differences only" if diff_only else "all flags"
    stats = f"{len(nodes)} node(s), {len(all_flags)} flag(s)"
    print()
    print(f"  {_bold_cyan(label)} {_dim('—')} {subtitle} {_dim('—')} {stats}")
    print()

    # Header
    header_cells = [_bold("FLAG")] + [_bold_yellow(n) for n in nodes]
    print(_hline(widths, _TL, _TJ, _TR))
    print(_row(header_cells, widths))
    print(_hline(widths, _LJ, _CJ, _RJ))

    # Rows
    for i, flag_name in enumerate(all_flags):
        uniform = _is_uniform(flag_name, flag_map)
        flag_label = _fg_cyan(flag_name) if uniform else _bold_yellow(flag_name)

        cells = [flag_label]
        for node in nodes:
            raw = flag_map[node].get(flag_name)
            if raw is None:
                cells.append(_dim("—"))
            elif not uniform:
                cells.append(_fg_yellow(_truncate(raw, node_col_w)))
            else:
                cells.append(_truncate(raw, node_col_w))

        print(_row(cells, widths))

        if i < len(all_flags) - 1 and not uniform:
            print(_hline(widths, _LJ, _CJ, _RJ))

    print(_hline(widths, _BL, _BJ, _BR))
    print()


def write_csv(
    out: io.TextIOBase,
    process_type: str,
    flag_map: FlagMap,
    diff_only: bool = False,
):
    if not flag_map:
        return
    nodes = sorted(flag_map.keys())
    all_flags = _all_flag_names(flag_map)
    if diff_only:
        all_flags = [f for f in all_flags if not _is_uniform(f, flag_map)]
    if not all_flags:
        return

    writer = csv.writer(out)
    writer.writerow(["process", "flag"] + nodes)
    for flag_name in all_flags:
        row = [process_type, flag_name]
        for node in nodes:
            row.append(flag_map[node].get(flag_name, ""))
        writer.writerow(row)


# ---------------------------------------------------------------------------
# Status output
# ---------------------------------------------------------------------------


def _stderr_status(msg: str):
    is_tty = hasattr(sys.stderr, "isatty") and sys.stderr.isatty()
    if is_tty and _USE_COLOR:
        print(f"\033[2m  ▸ {msg}\033[0m", file=sys.stderr)
    else:
        print(f"  {msg}", file=sys.stderr)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description="Extract and compare gflag settings from a "
        "YugabyteDB support bundle.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
examples:
  %(prog)s bundle.tar.gz
  %(prog)s ./extracted-bundle/
  %(prog)s bundle.tar.gz --diff          # only flags that differ across nodes
  %(prog)s bundle.tar.gz --csv out.csv   # write results to CSV
  %(prog)s bundle.tar.gz --process master  # only show master flags
""",
    )
    parser.add_argument(
        "bundle", help="Path to support bundle (.tar.gz) or extracted directory"
    )
    parser.add_argument(
        "--diff", "-d", action="store_true",
        help="Only show flags that differ across nodes",
    )
    parser.add_argument(
        "--process", "-p", choices=["master", "tserver"],
        help="Only show flags for this process type",
    )
    parser.add_argument(
        "--csv", dest="csv_file", metavar="FILE",
        help="Write output to a CSV file instead of a table",
    )
    parser.add_argument(
        "--width", "-w", type=int, default=0,
        help="Max column width for flag values (default: auto-fit terminal)",
    )
    parser.add_argument(
        "--no-color", action="store_true",
        help="Disable colored output",
    )
    args = parser.parse_args()

    global _USE_COLOR
    if args.no_color:
        _USE_COLOR = False

    bundle_path = args.bundle
    if not os.path.exists(bundle_path):
        parser.error(f"'{bundle_path}' does not exist.")

    _stderr_status(f"Scanning {bundle_path} …")

    if os.path.isdir(bundle_path):
        master_flags, tserver_flags = scan_directory(bundle_path)
    else:
        try:
            is_tar = tarfile.is_tarfile(bundle_path)
        except Exception:
            is_tar = False
        if is_tar:
            master_flags, tserver_flags = scan_tar(bundle_path)
        else:
            parser.error(
                f"'{bundle_path}' is not a recognised format "
                "(expected .tar.gz or a directory)."
            )

    total_nodes = sorted(set(list(master_flags.keys()) + list(tserver_flags.keys())))
    if not total_nodes:
        print("\n  No server.conf files found in the bundle.\n", file=sys.stderr)
        sys.exit(1)

    _stderr_status(
        f"Found {len(total_nodes)} node(s): "
        f"{len(master_flags)} master, {len(tserver_flags)} tserver"
    )

    if args.csv_file:
        with open(args.csv_file, "w", newline="", encoding="utf-8") as f:
            if args.process != "tserver":
                write_csv(f, "master", master_flags, diff_only=args.diff)
            if args.process != "master":
                write_csv(f, "tserver", tserver_flags, diff_only=args.diff)
        _stderr_status(f"Wrote CSV → {args.csv_file}")
    else:
        if args.process != "tserver":
            print_flag_table("master", master_flags, diff_only=args.diff, value_width=args.width)
        if args.process != "master":
            print_flag_table("tserver", tserver_flags, diff_only=args.diff, value_width=args.width)


if __name__ == "__main__":
    main()

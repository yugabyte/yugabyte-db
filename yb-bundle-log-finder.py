#!/usr/bin/env python3
"""
Find log files covering a specific timestamp in a YugabyteDB support bundle.

Searches through .tar.gz bundles (including nested archives and .gz-compressed
logs), or extracted directories, and reports which log files cover the target
time window grouped by node name and IP.

Requires only the Python 3 standard library.
"""

import argparse
import datetime
import gzip
import io
import json
import os
import re
import sys
import tarfile
from collections import defaultdict
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

CHUNK_SIZE = 64 * 1024  # bytes read from each end of a log file

# ---------------------------------------------------------------------------
# Timestamp extraction from log *lines*
# ---------------------------------------------------------------------------

GLOG_LINE_RE = re.compile(
    r"^[IWEF](\d{2})(\d{2})\s+(\d{2}):(\d{2}):(\d{2})(?:\.\d+)?"
)
ISO_LINE_RE = re.compile(
    r"(\d{4})-(\d{2})-(\d{2})[T ](\d{2}):(\d{2}):(\d{2})"
)
ROCKSDB_LINE_RE = re.compile(
    r"(\d{4})/(\d{2})/(\d{2})-(\d{2}):(\d{2}):(\d{2})"
)
SYSLOG_MONTHS = {
    "Jan": 1, "Feb": 2, "Mar": 3, "Apr": 4, "May": 5, "Jun": 6,
    "Jul": 7, "Aug": 8, "Sep": 9, "Oct": 10, "Nov": 11, "Dec": 12,
}
SYSLOG_LINE_RE = re.compile(
    r"^([A-Z][a-z]{2})\s+(\d{1,2})\s+(\d{2}):(\d{2}):(\d{2})"
)

# ---------------------------------------------------------------------------
# Date extraction from log *filenames*
# ---------------------------------------------------------------------------

GLOG_FNAME_RE = re.compile(r"\.(\d{4})(\d{2})(\d{2})-(\d{2})(\d{2})(\d{2})\.")
PG_FNAME_RE = re.compile(r"postgresql-(\d{4})-(\d{2})-(\d{2})_(\d{2})(\d{2})(\d{2})")
CONNMGR_FNAME_RE = re.compile(
    r"ysql-conn-mgr-(\d{4})-(\d{2})-(\d{2})_(\d{2})(\d{2})(\d{2})"
)
APP_LOG_FNAME_RE = re.compile(r"application-log-(\d{4})-(\d{2})-(\d{2})")
SYSLOG_FNAME_RE = re.compile(r"messages-(\d{4})(\d{2})(\d{2})")


# ---------------------------------------------------------------------------
# Data types
# ---------------------------------------------------------------------------


@dataclass
class LogFileInfo:
    path: str
    node_name: str
    service: str = "unknown"
    first_ts: Optional[datetime.datetime] = None
    last_ts: Optional[datetime.datetime] = None
    filename_ts: Optional[datetime.datetime] = None
    hostname: Optional[str] = None


# ---------------------------------------------------------------------------
# Service classification
# ---------------------------------------------------------------------------

SERVICE_ALIASES: Dict[str, str] = {
    "pg": "postgresql",
    "postgres": "postgresql",
    "ysql": "postgresql",
    "conn_mgr": "ysql-conn-mgr",
    "connmgr": "ysql-conn-mgr",
    "connection_pooling": "ysql-conn-mgr",
    "odyssey": "ysql-conn-mgr",
    "node_agent": "node-agent",
    "nodeagent": "node-agent",
    "ynp": "node-agent",
    "syslog": "system",
    "syslogs": "system",
    "messages": "system",
    "yb-master": "master",
    "yb-tserver": "tserver",
    "controller": "ybc",
    "app_log": "yba",
    "application": "yba",
    "platform": "yba",
}

KNOWN_SERVICES = frozenset((
    "master", "tserver", "postgresql", "ysql-conn-mgr",
    "node-agent", "ybc", "yba", "system",
))


def classify_service(path: str) -> str:
    """Determine which YB service a log file belongs to based on its path."""
    low = path.lower().replace("\\", "/")
    basename = os.path.basename(low)

    if basename.startswith("postgresql-") or basename.startswith("filtered_postgresql-"):
        return "postgresql"
    if basename.startswith("ysql-conn-mgr"):
        return "ysql-conn-mgr"
    if "yb-master" in basename:
        return "master"
    if "yb-tserver" in basename:
        return "tserver"
    if basename.startswith("messages"):
        return "system"
    if basename.startswith("application-log"):
        return "yba"

    if "/ynp/" in low or "/node-agent/" in low:
        return "node-agent"
    if "/ybc-data/" in low or "/controller/logs/" in low:
        return "ybc"
    if "/yba/" in low and "/application_logs/" in low:
        return "yba"

    if "/master/logs/" in low:
        return "master"
    if "/tserver/logs/" in low:
        return "tserver"

    return "other"


def resolve_service_filter(raw: str) -> frozenset:
    """Parse a comma-separated service filter, resolving aliases."""
    names = {s.strip().lower() for s in raw.split(",") if s.strip()}
    resolved = set()
    for name in names:
        canonical = SERVICE_ALIASES.get(name, name)
        resolved.add(canonical)
    unknown = resolved - KNOWN_SERVICES - {"other"}
    if unknown:
        raise ValueError(
            f"Unknown service(s): {', '.join(sorted(unknown))}. "
            f"Valid services: {', '.join(sorted(KNOWN_SERVICES))}"
        )
    return frozenset(resolved)


# ---------------------------------------------------------------------------
# Argument helpers
# ---------------------------------------------------------------------------


def parse_buffer(raw: str) -> datetime.timedelta:
    """Parse a human-friendly buffer like ``2h``, ``30m``, ``1d2h30m``."""
    s = raw.lstrip("+")
    m = re.fullmatch(r"(?:(\d+)d)?(?:(\d+)h)?(?:(\d+)m)?", s)
    if not m or not any(m.groups()):
        raise ValueError(
            f"Invalid buffer format: '{raw}'. "
            "Use combinations of Nd, Nh, Nm  (e.g. 2h, 30m, 1d2h30m)"
        )
    return datetime.timedelta(
        days=int(m.group(1) or 0),
        hours=int(m.group(2) or 0),
        minutes=int(m.group(3) or 0),
    )


def parse_target_timestamp(s: str) -> datetime.datetime:
    for fmt in (
        "%Y-%m-%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
        "%Y-%m-%d %H:%M",
        "%Y-%m-%dT%H:%M",
        "%Y-%m-%d",
        "%Y%m%d-%H%M%S",
        "%Y%m%d %H%M%S",
    ):
        try:
            return datetime.datetime.strptime(s, fmt)
        except ValueError:
            continue
    raise ValueError(f"Cannot parse timestamp: '{s}'. Use YYYY-MM-DD HH:MM:SS.")


# ---------------------------------------------------------------------------
# Timestamp extraction
# ---------------------------------------------------------------------------


def _safe_dt(*args):
    try:
        return datetime.datetime(*args)
    except ValueError:
        return None


def extract_ts_from_line(
    line: str, year_hint: int
) -> Optional[datetime.datetime]:
    """Return the first recognisable timestamp in *line*, or ``None``."""
    m = GLOG_LINE_RE.match(line)
    if m:
        month, day = int(m.group(1)), int(m.group(2))
        if 1 <= month <= 12 and 1 <= day <= 31:
            ts = _safe_dt(
                year_hint, month, day,
                int(m.group(3)), int(m.group(4)), int(m.group(5)),
            )
            if ts:
                return ts

    m = ISO_LINE_RE.search(line)
    if m:
        ts = _safe_dt(*(int(m.group(i)) for i in range(1, 7)))
        if ts:
            return ts

    m = ROCKSDB_LINE_RE.search(line)
    if m:
        ts = _safe_dt(*(int(m.group(i)) for i in range(1, 7)))
        if ts:
            return ts

    m = SYSLOG_LINE_RE.match(line)
    if m and m.group(1) in SYSLOG_MONTHS:
        ts = _safe_dt(
            year_hint,
            SYSLOG_MONTHS[m.group(1)],
            int(m.group(2)),
            int(m.group(3)), int(m.group(4)), int(m.group(5)),
        )
        if ts:
            return ts

    return None


def extract_date_from_filename(filename: str) -> Optional[datetime.datetime]:
    basename = os.path.basename(filename)
    for regex, groups in (
        (GLOG_FNAME_RE, 6),
        (PG_FNAME_RE, 6),
        (CONNMGR_FNAME_RE, 6),
    ):
        m = regex.search(basename)
        if m:
            ts = _safe_dt(*(int(m.group(i)) for i in range(1, groups + 1)))
            if ts:
                return ts
    m = APP_LOG_FNAME_RE.search(basename)
    if m:
        return _safe_dt(int(m.group(1)), int(m.group(2)), int(m.group(3)))
    m = SYSLOG_FNAME_RE.search(basename)
    if m:
        return _safe_dt(int(m.group(1)), int(m.group(2)), int(m.group(3)))
    return None


# ---------------------------------------------------------------------------
# File classification
# ---------------------------------------------------------------------------

_BINARY_EXTS = frozenset((
    ".tar", ".tar.gz", ".tgz", ".zip", ".jar", ".png", ".jpg", ".jpeg",
    ".gif", ".svg", ".ico", ".bin", ".dat", ".so", ".dylib", ".parquet",
))


def is_log_file(path: str) -> bool:
    low = path.lower()
    for ext in _BINARY_EXTS:
        if low.endswith(ext):
            return False
    basename = os.path.basename(low)
    if "log" in basename or basename.startswith("messages"):
        return True
    if "/logs/" in low or "/log/" in low:
        return True
    return False


# ---------------------------------------------------------------------------
# Content reading helpers
# ---------------------------------------------------------------------------


def _first_last_chunks_from_gz(gz_bytes: bytes) -> Tuple[bytes, bytes]:
    """Stream-decompress keeping only the first and last chunk in memory."""
    try:
        with gzip.GzipFile(fileobj=io.BytesIO(gz_bytes)) as f:
            first = f.read(CHUNK_SIZE)
            last = first
            while True:
                chunk = f.read(CHUNK_SIZE)
                if not chunk:
                    break
                last = chunk
            return first, last
    except Exception:
        return b"", b""


def _first_last_chunks(fileobj, size: int) -> Tuple[bytes, bytes]:
    if size <= 2 * CHUNK_SIZE:
        data = fileobj.read()
        return data, data
    first = fileobj.read(CHUNK_SIZE)
    fileobj.seek(max(0, size - CHUNK_SIZE))
    last = fileobj.read(CHUNK_SIZE)
    return first, last


def get_boundary_timestamps(
    first_bytes: bytes, last_bytes: bytes, year_hint: int
) -> Tuple[Optional[datetime.datetime], Optional[datetime.datetime]]:
    first_ts = last_ts = None
    try:
        for line in first_bytes.decode("utf-8", errors="replace").splitlines()[:300]:
            ts = extract_ts_from_line(line, year_hint)
            if ts:
                first_ts = ts
                break
    except Exception:
        pass
    try:
        for line in reversed(
            last_bytes.decode("utf-8", errors="replace").splitlines()[-300:]
        ):
            ts = extract_ts_from_line(line, year_hint)
            if ts:
                last_ts = ts
                break
    except Exception:
        pass
    return first_ts, last_ts


# ---------------------------------------------------------------------------
# Node / IP helpers
# ---------------------------------------------------------------------------


def extract_node_info(path: str) -> Tuple[str, Optional[str]]:
    parts = [p for p in path.replace("\\", "/").split("/") if p]

    node_name = "unknown"
    hostname = None

    for i, part in enumerate(parts):
        if part in ("master", "tserver") and i > 0:
            candidate = parts[i - 1]
            if not candidate.startswith("yb-support-bundle"):
                node_name = candidate
                break

    if node_name == "unknown":
        if "YBA" in parts:
            node_name = "YBA"
        else:
            for i, part in enumerate(parts):
                if part.startswith("yb-support-bundle") and i + 1 < len(parts):
                    node_name = parts[i + 1]
                    break

    basename = parts[-1] if parts else ""
    m = re.search(r"yb-(?:master|tserver)\.([^.]+)\.", basename)
    if m:
        hostname = m.group(1)

    return node_name, hostname


def _find_ips_in_json(data, node_ips: Dict[str, str], depth: int = 0):
    if depth > 10:
        return
    if isinstance(data, dict):
        name = ip = None
        for k in ("nodeName", "node_name"):
            if k in data and isinstance(data[k], str):
                name = data[k]
                break
        for k in ("private_ip", "privateIp", "ip", "ipAddress"):
            if k in data and isinstance(data[k], str):
                ip = data[k]
                break
        if not ip:
            ci = data.get("cloudInfo") or data.get("cloud_info")
            if isinstance(ci, dict):
                ip = ci.get("private_ip", "")
        if name and ip:
            node_ips[name] = ip
        for v in data.values():
            _find_ips_in_json(v, node_ips, depth + 1)
    elif isinstance(data, list):
        for item in data:
            _find_ips_in_json(item, node_ips, depth + 1)


def parse_json_for_ips(raw: bytes) -> Dict[str, str]:
    node_ips: Dict[str, str] = {}
    try:
        _find_ips_in_json(json.loads(raw.decode("utf-8", errors="replace")), node_ips)
    except (json.JSONDecodeError, AttributeError, TypeError):
        pass
    return node_ips


# ---------------------------------------------------------------------------
# Coverage check
# ---------------------------------------------------------------------------


def covers_timestamp(
    info: LogFileInfo,
    target: datetime.datetime,
    buffer: datetime.timedelta,
) -> bool:
    search_start = target - buffer
    search_end = target + buffer

    file_start = info.first_ts or info.filename_ts
    file_end = info.last_ts

    if file_start is None:
        return False

    if file_end is None or file_end < file_start:
        file_end = file_start + datetime.timedelta(hours=24)

    return file_start <= search_end and file_end >= search_start


# ---------------------------------------------------------------------------
# Bundle scanning
# ---------------------------------------------------------------------------


def _process_log_member(
    path: str,
    get_content,
    year_hint: int,
    target: datetime.datetime,
    buffer: datetime.timedelta,
    service_filter: Optional[frozenset] = None,
) -> Optional[LogFileInfo]:
    """Evaluate a single log file and return info if it covers the window."""
    if not is_log_file(path):
        return None

    service = classify_service(path)
    if service_filter and service not in service_filter:
        return None

    node_name, hostname = extract_node_info(path)
    filename_ts = extract_date_from_filename(path)
    effective_year = filename_ts.year if filename_ts else year_hint

    try:
        first_bytes, last_bytes = get_content()
    except Exception:
        return None

    first_ts, last_ts = get_boundary_timestamps(first_bytes, last_bytes, effective_year)

    info = LogFileInfo(
        path=path,
        node_name=node_name,
        service=service,
        first_ts=first_ts,
        last_ts=last_ts,
        filename_ts=filename_ts,
        hostname=hostname,
    )
    return info if covers_timestamp(info, target, buffer) else None


def _process_nested_tar(
    data: bytes,
    archive_label: str,
    target: datetime.datetime,
    buffer: datetime.timedelta,
    year_hint: int,
    service_filter: Optional[frozenset] = None,
) -> Tuple[List[LogFileInfo], Dict[str, str]]:
    results: List[LogFileInfo] = []
    node_ips: Dict[str, str] = {}
    try:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:*") as tf:
            for member in tf.getmembers():
                if not member.isfile():
                    continue
                full = f"{archive_label}/{member.name}"
                if member.name.endswith(".json"):
                    try:
                        f = tf.extractfile(member)
                        if f:
                            node_ips.update(parse_json_for_ips(f.read()))
                    except Exception:
                        pass

                def _content(m=member):
                    f = tf.extractfile(m)
                    if not f:
                        return b"", b""
                    if m.name.endswith(".gz"):
                        return _first_last_chunks_from_gz(f.read())
                    return _first_last_chunks(f, m.size)

                info = _process_log_member(
                    full, _content, year_hint, target, buffer, service_filter
                )
                if info:
                    results.append(info)
    except Exception:
        pass
    return results, node_ips


def process_tar_archive(
    tar_path: str,
    target: datetime.datetime,
    buffer: datetime.timedelta,
    service_filter: Optional[frozenset] = None,
) -> Tuple[List[LogFileInfo], Dict[str, str]]:
    results: List[LogFileInfo] = []
    node_ips: Dict[str, str] = {}
    year_hint = target.year
    scanned = 0

    with tarfile.open(tar_path, "r:*") as tf:
        members = tf.getmembers()

        # First pass: metadata / manifests for IP resolution
        for member in members:
            if member.isfile() and member.name.endswith(".json"):
                try:
                    f = tf.extractfile(member)
                    if f:
                        node_ips.update(parse_json_for_ips(f.read()))
                except Exception:
                    pass

        # Second pass: log files and nested archives
        for member in members:
            if not member.isfile():
                continue
            path = member.name

            if path.endswith((".tar.gz", ".tgz", ".tar")):
                try:
                    f = tf.extractfile(member)
                    if f:
                        nested_r, nested_i = _process_nested_tar(
                            f.read(), path, target, buffer, year_hint,
                            service_filter,
                        )
                        results.extend(nested_r)
                        node_ips.update(nested_i)
                        scanned += len(nested_r)
                except Exception:
                    pass
                continue

            def _content(m=member):
                f = tf.extractfile(m)
                if not f:
                    return b"", b""
                if m.name.endswith(".gz"):
                    return _first_last_chunks_from_gz(f.read())
                return _first_last_chunks(f, m.size)

            info = _process_log_member(
                path, _content, year_hint, target, buffer, service_filter
            )
            if info:
                results.append(info)
            scanned += 1
            if scanned % 200 == 0:
                print(f"  … scanned {scanned} files", file=sys.stderr)

    return results, node_ips


def process_directory(
    dir_path: str,
    target: datetime.datetime,
    buffer: datetime.timedelta,
    service_filter: Optional[frozenset] = None,
) -> Tuple[List[LogFileInfo], Dict[str, str]]:
    results: List[LogFileInfo] = []
    node_ips: Dict[str, str] = {}
    year_hint = target.year
    scanned = 0

    for root, _dirs, files in os.walk(dir_path):
        for fname in files:
            fpath = os.path.join(root, fname)
            rel = os.path.relpath(fpath, dir_path)

            if fname.endswith(".json"):
                try:
                    with open(fpath, "rb") as f:
                        node_ips.update(parse_json_for_ips(f.read()))
                except Exception:
                    pass

            if fname.endswith((".tar.gz", ".tgz", ".tar")):
                try:
                    with open(fpath, "rb") as f:
                        nested_r, nested_i = _process_nested_tar(
                            f.read(), rel, target, buffer, year_hint,
                            service_filter,
                        )
                        results.extend(nested_r)
                        node_ips.update(nested_i)
                except Exception:
                    pass
                continue

            def _content(_p=fpath, _n=fname):
                if _n.endswith(".gz"):
                    with open(_p, "rb") as f:
                        return _first_last_chunks_from_gz(f.read())
                size = os.path.getsize(_p)
                with open(_p, "rb") as f:
                    return _first_last_chunks(f, size)

            info = _process_log_member(
                rel, _content, year_hint, target, buffer, service_filter
            )
            if info:
                results.append(info)
            scanned += 1
            if scanned % 200 == 0:
                print(f"  … scanned {scanned} files", file=sys.stderr)

    return results, node_ips


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------


def _fmt_ts(ts: Optional[datetime.datetime]) -> str:
    return ts.strftime("%Y-%m-%d %H:%M:%S") if ts else "unknown"


def _fmt_delta(td: datetime.timedelta) -> str:
    total = int(td.total_seconds())
    if total == 0:
        return "0"
    parts = []
    d, rem = divmod(total, 86400)
    h, rem = divmod(rem, 3600)
    m, _ = divmod(rem, 60)
    if d:
        parts.append(f"{d}d")
    if h:
        parts.append(f"{h}h")
    if m:
        parts.append(f"{m}m")
    return "".join(parts) or "0"


def _resolve_display_path(
    log: LogFileInfo, bundle_path: str, is_directory: bool,
) -> str:
    """Build the display path for a log entry, relativised to the node name."""
    display = log.path
    parts = display.split("/")
    for i, p in enumerate(parts):
        if p == log.node_name:
            display = "/".join(parts[i:])
            break
    return display


def _resolve_grep_path(
    log: LogFileInfo, bundle_path: str, is_directory: bool,
) -> str:
    """Build an absolute on-disk path suitable for grep / rg.

    For extracted directories the path is real and resolvable.
    For tar archives we still emit the internal archive path (prefixed with
    the archive name) so the user can see what to extract.
    """
    if is_directory:
        return os.path.join(os.path.abspath(bundle_path), log.path)
    return log.path


def print_results(
    results: List[LogFileInfo],
    node_ips: Dict[str, str],
    target: datetime.datetime,
    buffer: datetime.timedelta,
    service_filter: Optional[frozenset] = None,
    grep_mode: bool = False,
    bundle_path: str = "",
    is_directory: bool = False,
):
    if grep_mode:
        if not results:
            print("# no matching log files found", file=sys.stderr)
            return
        for info in sorted(results, key=lambda x: x.path):
            print(_resolve_grep_path(info, bundle_path, is_directory))
        print(
            f"# {len(results)} file(s) matched", file=sys.stderr,
        )
        return

    search_start = target - buffer
    search_end = target + buffer
    buf_str = f" (buffer: ±{_fmt_delta(buffer)})" if buffer.total_seconds() > 0 else ""
    svc_str = f"  Services: {', '.join(sorted(service_filter))}" if service_filter else ""

    print(f"\n{'=' * 74}")
    print(f"  Logs covering {_fmt_ts(target)}{buf_str}")
    print(f"  Search window: {_fmt_ts(search_start)}  →  {_fmt_ts(search_end)}")
    if svc_str:
        print(svc_str)
    print(f"{'=' * 74}")

    if not results:
        print("\n  No matching log files found.\n")
        return

    by_node: Dict[str, List[LogFileInfo]] = defaultdict(list)
    for info in results:
        by_node[info.node_name].append(info)

    for node_name in sorted(by_node):
        logs = sorted(
            by_node[node_name],
            key=lambda x: x.first_ts or x.filename_ts or datetime.datetime.max,
        )
        ip = node_ips.get(node_name)
        if not ip:
            for log in logs:
                if log.hostname:
                    ip = log.hostname
                    break
        ip_str = ip or "N/A"

        print(f"\n  Node: {node_name}  |  IP: {ip_str}")
        print(f"  {'─' * 70}")

        for log in logs:
            display = _resolve_display_path(log, bundle_path, is_directory)
            start = log.first_ts or log.filename_ts
            end = log.last_ts
            svc_tag = f"  [{log.service}]"
            print(f"    {display}{svc_tag}")
            print(f"      covers: {_fmt_ts(start)}  →  {_fmt_ts(end)}")

    total = len(results)
    nodes = len(by_node)
    print(
        f"\n  Found {total} log file{'s' if total != 1 else ''} "
        f"across {nodes} node{'s' if nodes != 1 else ''}.\n"
    )


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(
        description="Find log files covering a specific timestamp in a "
        "YugabyteDB support bundle.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""\
examples:
  %(prog)s bundle.tar.gz "2024-03-15 14:30:00"
  %(prog)s bundle.tar.gz "2024-03-15 14:30:00" --buffer 2h
  %(prog)s bundle.tar.gz "2024-03-15T14:30:00" --buffer 1d2h30m
  %(prog)s ./extracted-bundle/ "2024-03-15 14:30:00" --buffer 30m
  %(prog)s bundle.tar.gz "2024-03-15 14:30:00" --service master,tserver
  %(prog)s bundle.tar.gz "2024-03-15 14:30:00" -s postgresql -b 2h
  %(prog)s ./extracted/ "2024-03-15 14:30:00" -b 2h --grep | xargs grep "ERROR"
  grep "tablet split" $(%(prog)s ./extracted/ "2024-03-15 14:30:00" -g -s tserver)

buffer format:
  Combine days (d), hours (h), and minutes (m):
    30m       30 minutes before and after
    2h        2 hours before and after
    1d        1 day before and after
    1d2h30m   1 day, 2 hours, 30 minutes before and after

services (comma-separated):
  master          YB-Master logs
  tserver         YB-TServer logs (glog, not postgresql/conn-mgr)
  postgresql      PostgreSQL / YSQL logs  (aliases: pg, postgres, ysql)
  ysql-conn-mgr   YSQL connection manager  (aliases: conn_mgr, connmgr)
  node-agent      Node agent / YNP logs    (aliases: node_agent, ynp)
  ybc             YB-Controller logs
  yba             YBA platform logs        (aliases: app_log, platform)
  system          OS syslog / messages     (aliases: syslog, messages)
""",
    )
    parser.add_argument(
        "bundle", help="Path to support bundle (.tar.gz) or extracted directory"
    )
    parser.add_argument(
        "timestamp", help='Target timestamp  (e.g. "2024-03-15 14:30:00")'
    )
    parser.add_argument(
        "--buffer", "-b", default="0m",
        help="Time buffer window  (e.g. 30m, 2h, 1d, 1d2h30m). Default: 0",
    )
    parser.add_argument(
        "--service", "-s", default=None,
        help="Comma-separated list of services to include "
        "(e.g. master,tserver,postgresql). See below for full list.",
    )
    parser.add_argument(
        "--grep", "-g", action="store_true",
        help="Output bare file paths (one per line) for use with "
        "grep / xargs / $(...). All other output goes to stderr.",
    )

    args = parser.parse_args()

    try:
        target_ts = parse_target_timestamp(args.timestamp)
    except ValueError as exc:
        parser.error(str(exc))
    try:
        buffer_td = parse_buffer(args.buffer)
    except ValueError as exc:
        parser.error(str(exc))

    service_filter = None
    if args.service:
        try:
            service_filter = resolve_service_filter(args.service)
        except ValueError as exc:
            parser.error(str(exc))

    bundle_path = args.bundle
    if not os.path.exists(bundle_path):
        parser.error(f"'{bundle_path}' does not exist.")

    print(f"Scanning bundle: {bundle_path}", file=sys.stderr)
    print(f"Target: {_fmt_ts(target_ts)}", file=sys.stderr)
    if buffer_td.total_seconds() > 0:
        print(f"Buffer: ±{_fmt_delta(buffer_td)}", file=sys.stderr)
    if service_filter:
        print(f"Services: {', '.join(sorted(service_filter))}", file=sys.stderr)

    is_dir = os.path.isdir(bundle_path)
    if is_dir:
        results, node_ips = process_directory(
            bundle_path, target_ts, buffer_td, service_filter
        )
    else:
        try:
            is_tar = tarfile.is_tarfile(bundle_path)
        except Exception:
            is_tar = False
        if is_tar:
            results, node_ips = process_tar_archive(
                bundle_path, target_ts, buffer_td, service_filter
            )
        else:
            parser.error(
                f"'{bundle_path}' is not a recognised bundle format "
                "(expected .tar.gz or a directory)."
            )

    print_results(
        results, node_ips, target_ts, buffer_td, service_filter,
        grep_mode=args.grep,
        bundle_path=bundle_path,
        is_directory=is_dir,
    )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generate the "All YSQL configuration parameters" reference page.

The page lists every YugabyteDB-specific (yb_*) YSQL configuration parameter that a
release exposes through pg_settings, so the list is exact for the release it is built
from rather than hand-maintained.

Usage:

    # 1. Start a cluster of the release the docs version tracks.
    docker run -d --name ybdocs yugabytedb/yugabyte:<version> \
        bin/yugabyted start --daemon=false --background=false

    # 2. Dump its parameters.
    docker exec ybdocs bin/ysqlsh -h 127.0.0.1 -U yugabyte -d yugabyte -tAc \
        "$(python3 gen-ysql-configuration-parameters.py --print-query)" > gucs.json

    # 3. Regenerate the page.
    python3 gen-ysql-configuration-parameters.py \
        --input gucs.json --version <version> \
        --output ../content/<docs-version>/reference/configuration/all-ysql-configuration-parameters.md

Re-run it for each release and commit the result.
"""

import argparse
import json
import os
import re
import sys
from collections import OrderedDict

QUERY = (
    "select coalesce(json_agg(row_to_json(s)),'[]'::json)::text from ("
    "select name, setting, boot_val, reset_val, unit, category, short_desc, extra_desc, "
    "context, vartype, min_val, max_val, enumvals, source "
    "from pg_settings where name like 'yb%' order by name) s"
)

# pg_settings categories collapsed into the areas the page is organized by. Anything not
# listed here falls through to "Other parameters", so a new category in a future release
# still shows up on the page instead of being dropped.
AREAS = OrderedDict([
    ("Query tuning and the optimizer", [
        "Query Tuning / Planner Method Configuration",
        "Query Tuning / Other Planner Options",
        "Query Tuning / Planner Cost Constants",
    ]),
    ("Transactions and statement behavior", [
        "Client Connection Defaults / Statement Behavior",
        "Version and Platform Compatibility / Previous PostgreSQL Versions",
        "Version and Platform Compatibility / Other Platforms and Clients",
        "Error Handling",
    ]),
    ("Locking", ["Lock Management"]),
    ("Observability and statistics", [
        "Statistics / Monitoring",
        "Reporting and Logging / When to Log",
    ]),
    ("Replication and change data capture", ["Replication / Sending Servers"]),
    ("Maintenance and resource usage", [
        "Autovacuum",
        "Resource Usage / Memory",
    ]),
])

# Areas that pg_settings categories don't distinguish. Matched on the parameter name and
# checked before the category, because these parameters are spread across categories that
# would otherwise scatter them into the catch-all area.
NAME_AREAS = OrderedDict([
    ("YSQL major version upgrade", re.compile(
        r"^yb_(extension_upgrade|major_version_upgrade_|mixed_mode_|upgrade_to_pg)")),
    # Extension parameters are namespaced as <extension>.<parameter>.
    ("Extension parameters", re.compile(r"\.")),
])

# A parameter is treated as internal when it says so itself. Keeping this a description
# match rather than a name list means the classification follows the source: if a
# parameter's description stops disclaiming user use, it moves to the main tables.
INTERNAL_DESC = re.compile(
    r"not to be touched by users|internal use|internal only|autoflag|"
    r"for testing|test only|do not use|do not modify",
    re.I,
)
TEST_NAME = re.compile(r"(^|\.)(yb_test_|TEST_)")

DEVELOPER_CATEGORY = "Developer Options"

# Corrections applied to the text pg_settings reports. Both maps exist so that the page
# doesn't publish a known-wrong description, and both are meant to shrink: fix the string
# in the source, then drop the entry here.
#
# Misspellings in short_desc/extra_desc, applied on word boundaries.
SPELLING = {
    "testint": "testing",
    "statmements": "statements",
    "transation": "transaction",
    "atleast": "at least",
    "herejust": "here just",
}

# Descriptions that are wrong rather than merely misspelled.
DESCRIPTION_OVERRIDES = {
    # Source string is copy-pasted from yb_pg_metrics.log_accesses. The parameter is
    # passed to the webserver as enable_tcmalloc_logging.
    "yb_pg_metrics.log_tcmalloc_stats":
        "Log TCMalloc memory statistics with each request received by the YSQL webserver.",
}

SPELLING_RE = re.compile(r"\b(%s)\b" % "|".join(SPELLING))


def classify(param):
    """Return "developer", "internal", or "user" for a parameter."""
    if param["category"] == DEVELOPER_CATEGORY or TEST_NAME.search(param["name"]):
        return "developer"
    if INTERNAL_DESC.search(param["short_desc"] or "") or param["context"] == "internal":
        return "internal"
    return "user"


def area_of(param):
    for area, pattern in NAME_AREAS.items():
        if pattern.search(param["name"]):
            return area
    for area, categories in AREAS.items():
        if param["category"] in categories:
            return area
    return "Other parameters"


def clean(text):
    return re.sub(r"\s+", " ", (text or "").strip())


def description(param):
    """Description cell for a parameter.

    Corrections are display-only: classify() still reads the raw source text, so fixing a
    typo here can't silently move a parameter between the page's sections.
    """
    if param["name"] in DESCRIPTION_OVERRIDES:
        text = DESCRIPTION_OVERRIDES[param["name"]]
    else:
        parts = [clean(param["short_desc"])]
        extra = clean(param["extra_desc"])
        if extra:
            parts.append(extra)
        text = " ".join(p.rstrip(".") + "." for p in parts if p)
        text = SPELLING_RE.sub(lambda m: SPELLING[m.group(1)], text)

    if param["vartype"] == "enum" and param["enumvals"]:
        values = ", ".join("`%s`" % v for v in param["enumvals"])
        text += " Values: %s." % values
    return text


def default_of(param):
    value = param["boot_val"]
    if value is None:
        return "n/a"
    if value == "":
        return "empty"
    return "`%s`" % value


def unit_of(param):
    return "`%s`" % param["unit"] if param["unit"] else "n/a"


def linked_name(name, anchors):
    anchor = name.replace("_", "-").replace(".", "")
    if anchor in anchors:
        return "[%s](../yb-tserver/#%s)" % (name, anchor)
    return name


def table(params, anchors):
    rows = [
        "| Parameter | Description | Type | Default | Unit | Context |",
        "| :--- | :--- | :--- | :--- | :--- | :--- |",
    ]
    for p in sorted(params, key=lambda x: x["name"]):
        rows.append("| %s | %s | %s | %s | %s | %s |" % (
            linked_name(p["name"], anchors),
            description(p),
            p["vartype"],
            default_of(p),
            unit_of(p),
            p["context"],
        ))
    return "\n".join(rows)


def find_anchors(tserver_page):
    """Anchors of parameters that already have a prose entry on the yb-tserver page."""
    if not tserver_page or not os.path.exists(tserver_page):
        return set()
    with open(tserver_page) as handle:
        headings = re.findall(r"^#{2,6}\s+(yb_[a-z0-9_]+)", handle.read(), re.M)
    return {h.replace("_", "-") for h in headings}


HEADER = """---
title: All YSQL configuration parameters
headerTitle: All YSQL configuration parameters
linkTitle: All YSQL parameters
description: Reference list of all YugabyteDB-specific YSQL configuration parameters.
menu:
  {docs_version}:
    identifier: all-ysql-configuration-parameters
    parent: configuration
    weight: 2460
type: docs
showRightNav: true
---

YSQL supports the PostgreSQL [server configuration parameters](https://www.postgresql.org/docs/15/runtime-config.html), plus the YugabyteDB-specific parameters listed on this page. This page covers every `yb_` parameter that {version} exposes. Parameters that need more than a one-line description also have an entry under [YSQL configuration parameters](../yb-tserver/#ysql-configuration-parameters) on the YB-TServer reference page; the parameter names below link to that entry where one exists.

To see the parameters and their current values on a running cluster, query `pg_settings`:

```sql
SELECT name, setting, unit, context, short_desc FROM pg_settings WHERE name LIKE 'yb\\_%';
```

## Setting a parameter

You can set a parameter at cluster, database, role, session, or statement scope. Narrower scopes override wider ones. For the precedence rules and the equivalent yb-tserver flags, see [How to modify configuration parameters](../yb-tserver/#how-to-modify-configuration-parameters).

```sql
ALTER DATABASE mydb SET yb_fetch_row_limit = 2048;    -- per database
ALTER ROLE myrole SET yb_fetch_row_limit = 2048;      -- per role
SET yb_fetch_row_limit = 2048;                        -- current session
SET LOCAL yb_fetch_row_limit = 2048;                  -- current transaction
```

To set a parameter for the whole cluster, use the yb-tserver [--ysql_pg_conf_csv](../yb-tserver/#ysql-pg-conf-csv) flag, for example `--ysql_pg_conf_csv=yb_fetch_row_limit=2048`.

## Reading the tables

| Column | Meaning |
| :--- | :--- |
| Parameter | Parameter name, as it appears in `pg_settings`. |
| Description | Description reported by `pg_settings.short_desc` and `extra_desc`. |
| Type | `bool`, `integer`, `real`, `string`, or `enum`. |
| Default | Built-in default (`pg_settings.boot_val`). A deployment can start with a different value if it is set using a flag, so check `pg_settings.reset_val` on your cluster. |
| Unit | Unit the value is interpreted in, where the parameter has one. |
| Context | When the parameter can be set. See the following table. |

The context determines who can change a parameter and whether a restart is needed.

| Context | Who can set it | Takes effect |
| :--- | :--- | :--- |
| `user` | Any user, for their own session | Immediately |
| `superuser` | Superusers only | Immediately |
| `backend` | Set when the connection is established | At connection start |
| `sighup` | Cluster configuration only (yb-tserver flag) | On configuration reload; no restart needed |
| `postmaster` | Cluster configuration only (yb-tserver flag) | Requires a restart of the YSQL process |
| `internal` | Read-only | Cannot be changed |

Parameters whose description begins with DEPRECATED are kept so that existing configurations keep working. Don't use them in new deployments.
"""

FOOTER_INTERNAL = """
## Internal parameters

{{{{< warning title="Not for production use" >}}}}
YugabyteDB sets these parameters itself, or reserves them for internal and upgrade workflows. Their descriptions state that they are not intended to be set by users. Set them only when Yugabyte Support asks you to.
{{{{< /warning >}}}}

{table}
"""

FOOTER_DEVELOPER = """
## Developer and test parameters

{{{{< warning title="Not for production use" >}}}}
These parameters change internal behavior, exist to support testing and debugging, and can change or be removed in any release. Set them only when Yugabyte Support asks you to.
{{{{< /warning >}}}}

{table}
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-query", action="store_true",
                        help="print the pg_settings query and exit")
    parser.add_argument("--input", help="JSON produced by the pg_settings query")
    parser.add_argument("--output", help="Markdown file to write")
    parser.add_argument("--version", help="release the data was collected from, e.g. 2026.1.1.1")
    parser.add_argument("--docs-version", default="stable",
                        help="docs version folder used in the menu front matter")
    parser.add_argument("--tserver-page",
                        help="path to yb-tserver.md, used to link parameters that have a "
                             "prose entry there")
    args = parser.parse_args()

    if args.print_query:
        print(QUERY)
        return 0

    for required in ("input", "output", "version"):
        if not getattr(args, required):
            parser.error("--%s is required" % required)

    with open(args.input) as handle:
        params = json.load(handle)

    anchors = find_anchors(args.tserver_page)

    buckets = {"user": [], "developer": [], "internal": []}
    for param in params:
        buckets[classify(param)].append(param)

    areas = OrderedDict((area, []) for area in AREAS)
    for area in NAME_AREAS:
        areas[area] = []
    areas["Other parameters"] = []
    for param in buckets["user"]:
        areas[area_of(param)].append(param)

    out = [HEADER.format(version="v" + args.version, docs_version=args.docs_version)]
    for area, members in areas.items():
        if not members:
            continue
        out.append("\n## %s\n" % area)
        out.append(table(members, anchors))
        out.append("")

    if buckets["internal"]:
        out.append(FOOTER_INTERNAL.format(table=table(buckets["internal"], anchors)))
    if buckets["developer"]:
        out.append(FOOTER_DEVELOPER.format(table=table(buckets["developer"], anchors)))

    with open(args.output, "w") as handle:
        handle.write("\n".join(out).rstrip() + "\n")

    print("Wrote %s: %d parameters (%d documented, %d internal, %d developer/test)" % (
        args.output, len(params), len(buckets["user"]),
        len(buckets["internal"]), len(buckets["developer"])), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

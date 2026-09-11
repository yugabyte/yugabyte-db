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
from collections import Counter, OrderedDict

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

# Areas keyed on the parameter name, checked before the category. Extension parameters
# share the catch-all "Customized Options" category, so only the name separates them.
NAME_AREAS = OrderedDict([
    # Extension parameters are namespaced as <extension>.<parameter>.
    ("Extension parameters", re.compile(r"\.")),
])

# The page lists only parameters a user would tune on a working cluster. Everything the
# following rules match is left off it.
#
# Internal parameters, identified by what their own description says. Keeping this a
# description match rather than a name list means the rule follows the source: if a
# parameter stops disclaiming user use, it appears on the page.
INTERNAL_DESC = re.compile(
    r"not to be touched by users|internal use|internal only|autoflag|"
    r"for testing|test only|do not use|do not modify",
    re.I,
)
TEST_NAME = re.compile(r"(^|\.)(yb_test_|TEST_)")

DEVELOPER_CATEGORY = "Developer Options"

# Parameters kept only so that existing configurations keep working.
DEPRECATED_DESC = re.compile(r"deprecated", re.I)

# Upgrade plumbing, set by the YSQL major version upgrade process rather than by a user.
UPGRADE_NAME = re.compile(
    r"^yb_(extension_upgrade|major_version_upgrade_|mixed_mode_|upgrade_to_pg)")

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


def excluded(param):
    """Reason this parameter is left off the page, or None to list it.

    These rules seeded the allowlist; the allowlist, not this function, decides what the
    page publishes. They stay so that --audit can explain why a parameter that a release
    exposes is not in the allowlist.
    """
    if param["category"] == DEVELOPER_CATEGORY or TEST_NAME.search(param["name"]):
        return "developer/test"
    if INTERNAL_DESC.search(param["short_desc"] or "") or param["context"] == "internal":
        return "internal"
    if DEPRECATED_DESC.search(param["short_desc"] or ""):
        return "deprecated"
    if UPGRADE_NAME.search(param["name"]):
        return "upgrade plumbing"
    return None


def read_allowlist(path):
    """Parameter names the page is allowed to publish, in file order."""
    names = []
    with open(path) as handle:
        for line in handle:
            line = line.split("#", 1)[0].strip()
            if line:
                names.append(line)
    return names


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
    """Description text for a parameter.

    Corrections are display-only: excluded() still reads the raw source text, so fixing a
    typo here can't silently pull an excluded parameter onto the page.
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
        return "none"
    if value == "":
        return "empty"
    return "`%s`" % value


def detail_link(name, anchors):
    anchor = name.replace("_", "-").replace(".", "")
    if anchor in anchors:
        return "For more detail, see [%s](../yb-tserver/#%s)." % (name, anchor)
    return None


def entries(params, anchors):
    """Render parameters as one section each.

    A six-column table is wider than the content area once parameter names and
    descriptions are in it, so each parameter gets a heading plus a tags row instead. The
    tags row is a wrapping flex container, so each metadata paragraph becomes a chip that
    reflows on narrow screens.
    """
    out = []
    for p in sorted(params, key=lambda x: x["name"]):
        out.append("##### %s\n" % p["name"])
        out.append("{{% tags/wrap %}}")
        # Hugo only parses the shortcode's inner content as markdown when it starts on its
        # own block, so the blank line here is load-bearing: without it the first metadata
        # line renders as literal text.
        out.append("")
        if p["context"] == "postmaster":
            out.append("{{<tags/feature/restart-needed>}}")
            out.append("")
        out.append("Default: %s" % default_of(p))
        out.append("")
        out.append("Type: `%s`" % p["vartype"])
        if p["unit"]:
            out.append("")
            out.append("Unit: `%s`" % p["unit"])
        out.append("")
        out.append("Context: `%s`" % p["context"])
        out.append("{{% /tags/wrap %}}")
        out.append("")
        out.append(description(p))
        link = detail_link(p["name"], anchors)
        if link:
            out.append("")
            out.append(link)
        out.append("")
    return "\n".join(out)


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

{{{{< warning title="Advanced parameters" >}}}}
Most deployments should not need to change these parameters. The defaults are chosen to suit the majority of workloads, and changing a parameter without understanding its effect can degrade performance, correctness, or stability. Change one only when you have a specific reason to, and test the change before applying it in production.
{{{{< /warning >}}}}

YSQL supports the PostgreSQL [server configuration parameters](https://www.postgresql.org/docs/15/runtime-config.html), plus the YugabyteDB-specific parameters listed on this page. Frequently used parameters are documented in detail under [YSQL configuration parameters](../yb-tserver/#ysql-configuration-parameters) on the YB-TServer reference page; entries below link to that page where such an entry exists.

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

## Reading the entries

Each parameter below lists the following:

- **Default** - the built-in default (`pg_settings.boot_val`). A deployment can start with a different value if it is set using a flag, so check `pg_settings.reset_val` on your cluster.
- **Type** - `bool`, `integer`, `real`, `string`, or `enum`.
- **Unit** - the unit the value is interpreted in, where the parameter has one.
- **Context** - when the parameter can be set, as described in the following table.
- {{{{% tags/feature/restart-needed %}}}} - the parameter can only be set in the cluster configuration, and the YSQL process must be restarted for a change to take effect.

The context determines who can change a parameter and whether a restart is needed.

"""

# Only the contexts that the listed parameters actually use are described on the page.
CONTEXT_HELP = OrderedDict([
    ("user", ("Any user, for their own session", "Immediately")),
    ("superuser", ("Superusers only", "Immediately")),
    ("backend", ("Set when the connection is established", "At connection start")),
    ("sighup", ("Cluster configuration only (yb-tserver flag)",
                "On configuration reload; no restart needed")),
    ("postmaster", ("Cluster configuration only (yb-tserver flag)",
                    "Requires a restart of the YSQL process")),
    ("internal", ("Read-only", "Cannot be changed")),
])


def context_table(params):
    used = {p["context"] for p in params}
    rows = [
        "| Context | Who can set it | Takes effect |",
        "| :--- | :--- | :--- |",
    ]
    for context, (who, when) in CONTEXT_HELP.items():
        if context in used:
            rows.append("| `%s` | %s | %s |" % (context, who, when))
    return "\n".join(rows)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--print-query", action="store_true",
                        help="print the pg_settings query and exit")
    parser.add_argument("--input", help="JSON produced by the pg_settings query")
    parser.add_argument("--output", help="Markdown file to write")
    parser.add_argument("--version", help="release the data was collected from, e.g. 2026.1.1.1")
    parser.add_argument("--docs-version", default="stable",
                        help="docs version folder used in the menu front matter")
    parser.add_argument("--allowlist",
                        default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                             "ysql-parameters-allowlist.txt"),
                        help="file listing the parameters the page may publish")
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

    allowed = read_allowlist(args.allowlist)
    by_name = {p["name"]: p for p in params}

    listed = [by_name[n] for n in allowed if n in by_name]

    # A release can add, rename, or remove parameters. Report both directions so neither
    # goes unnoticed: an unlisted parameter is never published silently, and a stale
    # allowlist entry is not silently ignored.
    unlisted = [p for p in params if p["name"] not in set(allowed)]
    missing = [n for n in allowed if n not in by_name]

    areas = OrderedDict((area, []) for area in AREAS)
    for area in NAME_AREAS:
        areas[area] = []
    areas["Other parameters"] = []
    for param in listed:
        areas[area_of(param)].append(param)

    out = [HEADER.format(docs_version=args.docs_version).rstrip(), "",
           context_table(listed), ""]
    for area, members in areas.items():
        if not members:
            continue
        out.append("\n## %s\n" % area)
        out.append(entries(members, anchors))
        out.append("")

    with open(args.output, "w") as handle:
        handle.write("\n".join(out).rstrip() + "\n")

    print("Wrote %s from v%s: %d of %d parameters listed" % (
        args.output, args.version, len(listed), len(params)), file=sys.stderr)

    reasons = Counter(excluded(p) or "not in allowlist" for p in unlisted)
    for reason, count in sorted(reasons.items()):
        print("  not published: %-18s %d" % (reason, count), file=sys.stderr)

    review = [p["name"] for p in unlisted if not excluded(p)]
    if review:
        print("\n  %d parameter(s) this release exposes are not in the allowlist and do "
              "not match an exclusion rule.\n  Add them to %s or leave them off "
              "deliberately:" % (len(review), args.allowlist), file=sys.stderr)
        for name in review:
            print("    %s" % name, file=sys.stderr)
    if missing:
        print("\n  %d allowlist entry/entries not present in this release (renamed or "
              "removed?):" % len(missing), file=sys.stderr)
        for name in missing:
            print("    %s" % name, file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())

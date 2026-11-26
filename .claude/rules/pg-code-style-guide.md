---
paths:
  - "src/postgres/**"
  - "src/postgres/**/*"
---

# PostgreSQL-derived code

You are changing code under `src/postgres`, a fork of upstream PostgreSQL that
is re-merged on every PG major-version upgrade.  Ordinary-looking edits here
routinely break that merge or get rejected by `src/lint` and code review.

Load the `pg-code-style-guide` skill and follow it.  It carries the fork
conventions: yb prefixes on YB-introduced identifiers, clustering YB additions
behind marker comments, keeping upstream-owned lines byte-identical, and the
regress test rules (upstream-named files, `yb.port.*`, `yb.orig.*`).

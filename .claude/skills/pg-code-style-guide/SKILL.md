---
name: pg-code-style-guide
description: >-
  The code style guide for YugabyteDB's PostgreSQL fork: required
  conventions for ANY change under src/postgres -- follow upstream
  PostgreSQL's rules, explicit and implicit, while isolating and marking
  the YB delta so that the diff/merge tooling (git, Phorge, GitHub) and
  future PG major-version merges are not confused.  Follow it when writing or
  editing backend C files or headers (planner, executor, catalog, commands,
  utils, yb_*.c), adding or wiring a GUC, adding a field to a PG-owned
  struct, adding an #include, creating or updating regress tests and
  expected .out files (yb.port.*, yb.orig.*), cherry-picking or merging
  upstream PostgreSQL commits, cleanup-only passes bringing an existing
  file up to these conventions, and reviewing or fixing a src/postgres diff
  before arc diff or when lint reports missing_yb_marker_for_yb_changes or
  another src/postgres check.  These requests look like ordinary edits,
  but code written from general PostgreSQL knowledge violates the fork's
  conventions and gets rejected in lint or review -- follow this guide
  instead.  Applies only when changing src/postgres code: not when merely
  reading or explaining that code, and not for areas outside the fork
  that just sound related (tserver/pggate gflags, odyssey, live-cluster
  ysqlsh operations, benchmarking).
---

# src/postgres code style guide

Everything under `src/postgres` is a fork of upstream PostgreSQL that gets
re-merged on every PG major-version upgrade.  The PG 11->15 merge hit 600+
conflicts (~2.3 per work day for a year), and every conflict avoided is
catchup time saved.  Every rule here serves one of two goals:

1. **Keep the YB delta small, clustered, and visibly marked** so a merger
   (human or git) can tell YB code from PG-owned code at a glance.
2. **Keep PG-owned lines byte-identical to upstream** so git auto-merges
   them and conflicts appear only where a real logical conflict exists.

The pinned upstream version of any file is defined by
`src/lint/upstream_repositories.csv`.  To see the YB delta of a file, or
to fetch the pinned upstream copy itself:

```bash
src/lint/diff_file_with_upstream.py <file> -b
src/lint/upstream_file.py <file> <output-path>
```

Older files predate these conventions and violate them freely (e.g.
createplan.c is full of interleaved YB code).  Surrounding code is NOT
precedent: follow the guide for the lines you add or change.  Do not go
fixing violations elsewhere in the file -- that is a refactor and belongs
in a cleanup-only revision (below) -- but when your change lands right on
one (say, appending declarations to a YB cluster that never got its
`/* YB declarations */` opener), fixing it in place is allowed.

## Add a yb prefix

Put `yb` in the name of every YB-introduced identifier that is visible
from upstream context: functions, global and file-scope variables, struct
fields, types, macros, GUC variables, parameters added to upstream
functions.  Normally `yb` is the prefix; where a fixed prefix is imposed,
it comes right after (`pg_yb_...` catalog names, `_YB_...` keyword
tokens).  Match the casing style of the surroundings.  The marker is what
lets a merger (and lint) attribute a line to YB without archaeology.

- Markers flag the YB/PG boundary, not every line inside it.  Within
  code that is already YB by context -- the body of a
  yb-prefixed function (`yb_foo`, `is_yb_foo`, `YbFoo`, ...), a
  YB-introduced file, or a block gated by a YB condition
  (`IsYugaByteEnabled()`, `IsYBRelation(...)`, a `yb_*` GUC) -- local
  variables take plain PG-style names and comments take no `YB:` prefix:
  the enclosing context already attributes them, and marking some lines
  but not others is noisier than marking none.  Use `/* YB: ... */`
  markers only where YB code sits directly in PG-owned context and the
  marker is what attributes it.
- Do not introduce new `ybc`-prefixed names: that namespace belongs to
  pggate, whose `YbcPg*` types src/postgres code uses everywhere.
  (`ybctid` is not one of them: it parses as yb + `ctid`.)  When
  extending an existing ybc-named family (e.g. the `ybcin*` index-AM
  handlers), match the family rather than stick out.
- YB-introduced files must be recognizable by name: start the file name
  with `yb` (`pg_yb` only where a constraint forces it, such as catalog
  table filenames), and every type they define must contain `yb` (any
  casing: `yb`/`Yb`/`YB`).  File names take the casing of the upstream
  family they join -- executor nodes are camel, `nodeHashjoin.c` ->
  `nodeYbBatchedNestloop.c`, today the only such family (lint allows `Yb`
  in a file name there and nowhere else).
  YB-introduced files must NOT contain a `/* YB includes */` block -- the
  whole file is YB, so includes are just normal includes.

## Cluster YB code

Group YB code separately from PG code.  Upstream constantly appends to its
own lists -- includes, declarations, struct fields, function parameters,
functions -- and a YB line sitting inside one turns every upstream append
into a conflict.  Do not let a YB
addition sit directly against a PG-owned line: put it in a YB block opened
by a marker comment and separated from PG code by a blank line.

- `/* YB includes */` -- one block after the upstream includes, holding
  every YB-added include; lint enforces the exact format and the
  `postgres.h`/`c.h` goes-first exception.
- `/* YB declarations */` -- for a block of YB static/extern declarations.
  The block goes at the end of the upstream declaration list -- in a
  header, at the bottom of the file (before the closing `#endif`); append
  to the YB block already there if one exists.  Declarations stay bare:
  PG puts the descriptive comment on the definition, not the declaration.
- `/* YB fields */` -- for YB fields appended to a PG-owned struct,
  enum, or union (append at the end unless field order is semantically
  constrained).  Keep PG's member-comment split: at most a short
  one-liner on the member itself; a longer description goes in a `YB:`
  paragraph appended at the bottom of the type's header comment, before
  the closing `*/` -- the upstream comment lines themselves stay
  untouched.
- Functions: place new YB functions at the bottom of the file (or in an
  existing YB function cluster), not between two upstream functions.
  If a static YB function is called from code above it, add a prototype to
  the `/* YB declarations */` block instead of moving the definition up.
  Exception: a yb function factored out of an upstream function stays
  where the upstream code was when that keeps the diff vs upstream
  smaller.
- Function parameters: cluster YB parameters at the end of the upstream
  parameter list and yb-prefix them; do not insert into the middle if
  avoidable.

Interleaving is otherwise allowed only when an ordering constraint forces
it -- a sorted list (e.g. `kwlist.h` keywords) or semantically ordered
entries.
Each interleaved YB entry then carries its own marker: a yb-prefixed
identifier (as in `_YB_ACCOUNT_P`) or, if the entry has no identifier, an
inline `/* YB */` comment.

Convention clusters YB blocks at the bottom, after the corresponding
upstream section.  Upstream tends to append its own new code there too,
so conflicts at the seam are expected.  The marker comment and the blank
line are what make them quick to attribute and resolve -- never skip
them.

## Do not touch PG-owned lines

Try to keep PG-owned lines exactly byte-identical.  The one allowance,
when a YB change must wrap them, is an indentation-only change --
`diff_file_with_upstream.py -b` and merge tooling can see through that.

- Do not fix upstream style, typos, or whitespace -- if the style is
  wrong, leave it wrong.  Trailing-whitespace "cleanup" of PG-owned lines
  has caused real merge conflicts.
- When a YB `if`/`else` must wrap PG-owned lines, do a pure indent: add
  leading tabs only, and do not rewrap the lines to fit 80 columns --
  rewrapped lines defeat the indentation-blind tooling.
- Avoid refactors and style changes of PG code in a feature diff,
  especially while a PG major-version merge is ongoing (save them for
  after it completes).  A needed code move gets its own revision, as a
  pure move separate from any modification, so git rename detection and
  bisect keep working.

## Avoid brittle tests

The regress test file taxonomy -- upstream-named files, `yb.port.*`,
`yb.orig.*`, `yb.depd.*`, schedule ordering, YB-deviation marking -- is
documented in `src/postgres/src/test/regress/README`.  Read it before
touching test files.  Do not edit a test with an upstream name to make a
YB behavior pass: the YB difference belongs in the `yb.port.*` version,
marked with a `-- YB: <reason>` comment.  (Lint pins upstream-named files
to `src/lint/upstream_repositories.csv`, which also scopes exceptions
such as pg_hint_plan.)

Avoid test assertions that you expect will break on a PG major-version
upgrade:

- Never assert absolute catalog versions.
- OIDs: [8000,10000) are YB-owned and never change; [10000,16384) are
  dynamically allocated by initdb and change; >= 16384 are user-allocated
  and change for many reasons.  OIDs < 8000 can in some cases change, but
  you can assume they won't and hardcode them if you must.
- Prefer structural or relative assertions (row counts, plan shapes,
  ratios) over environment-dependent absolutes.

## Better documentation

If the merger knows your intention, conflict resolution gets easier.  Say
WHY the YB change exists, and put the why where it belongs:

- In a code comment when it holds into the future: an invariant, the
  reason an expected output differs from upstream.
- In the commit message when it holds only at that moment: the
  motivation, what was verified.
- Make test plans easy for a stranger to run: specific commands, exact
  test names (no typos or partial specifications), no jargon, no assumed
  local setup.

## Cleanup-only revisions

Bringing an existing file up to these conventions -- adding missing yb
prefixes, `YB:` comments, and marker blocks, moving YB functions to the
bottom -- is legitimate work, but it is a refactor: make it a dedicated
revision, never part of a feature diff.  Cleanup has its own rules:

- The scope is the YB delta only.  PG-owned lines stay byte-identical:
  upstream style, typos, and whitespace remain out of bounds even in a
  cleanup pass.
- Stay behavior-preserving and mechanically checkable.  Moved code must be
  byte-identical at its new location so diff tooling can prove the move
  pure; a rename updates the definition and its references and nothing
  else.  If code needs both moving and editing, split them into separate
  revisions.
- Verify that `diff_file_with_upstream.py` output changed only in the
  cleaned aspects, that the lint warning count went down, and that the
  file still builds and passes its regress tests.
- During an active PG major-version merge, coordinate with the merge
  owners before cleaning up: cleanup churns exactly the lines the merge is
  resolving, so one-sided style fixes manufacture conflicts.

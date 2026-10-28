# yb_xcluster_ddl_replication

## Extension overview

This extension helps replicate DDLs for xCluster automatic mode.  xCluster
replicates WAL records, which contain limited information about schema changes
for tables and miss out completely on PG-catalog-only changes.  This extension
covers that gap by capturing entire DDL query strings, which can then be run on
the target side.

The extension is created in each database under replication, on both the
source and the target universes: each side has its own copy of the extension
and of its tables.

The extension does not replay DDLs itself.  Its jobs are:

- **On the source:** observe each DDL as it runs, decide whether it should be
  replicated, and if so record it into the `ddl_queue` table.
- **On the target:** block user-issued DDLs (the target catalog may only be
  changed by replication), let DDLs replayed by the ddl_queue handler pass
  through, and record each applied DDL into the `replicated_ddls` table.

The actual replay on the target is driven by the xCluster ddl_queue handler
(`xcluster_ddl_queue_handler.cc`), which reads `ddl_queue`, sets a few session
variables, and re-executes each captured query.  That handler lives outside of
this extension.

## Extension tables

Both tables are created by the extension install script.

| Table | Replicated? | Purpose |
|---|---|---|
| `ddl_queue` | Yes (source to target) | One row per source DDL: `(ddl_end_time, query_id, yb_data)`.  `yb_data` is a JSONB blob with the query string, command tag, user, schema, and OID-assignment maps. |
| `replicated_ddls` | No (separate copy per database) | Records DDLs already performed on this database.  A DDL still needs to run on the target iff it has a row in `ddl_queue` but no matching row here. |

`replicated_ddls` also holds one special row with key `(1, 1)`, which is used
to track DDL commit times for the ddl_queue handler to process.

`replicated_ddls` also exists on the source in order to handle switchovers:
after a switchover the old source becomes a target, and its ddl_queue handler
will then begin to process DDLs in `ddl_queue`.  We don't want the handler to
rerun the old DDLs from when this was the source, so the source writes each
DDL to its own `replicated_ddls` at the same time as `ddl_queue`.  After the
switchover, the handler then sees all of those DDLs as already processed.

## Background: how Postgres executes DDLs

The extension's structure follows directly from how Postgres runs statements.

### Query strings and statements

A client sends Postgres a *query string*, which may contain more than one SQL
statement (e.g., `CREATE TABLE t1 (); CREATE TABLE t2 ();`).  Postgres parses
the entire string up front into one parse tree per statement, then executes the
statements one at a time.  Each parse tree records the offset and length of its
statement within the original string (`stmt_location`, `stmt_len`); slicing the
string with those is the only way to recover the text of a single statement out
of a multi-statement string.

Statements that the planner knows how to optimize (SELECT, INSERT, UPDATE,
DELETE, MERGE) go through the planner and executor.  Every other statement
(which includes all DDLs) is called a *utility statement* and is executed by
a single dispatch function, `standard_ProcessUtility`.

### The ProcessUtility hook

Postgres exposes a hook variable, `ProcessUtility_hook`.  When an extension
sets it, Postgres calls the hook *instead of* `standard_ProcessUtility` for
every utility statement, and the hook is responsible for calling
`standard_ProcessUtility` itself.  The hook can therefore run code before the
statement executes, invoke the real implementation, and run code after it
finishes: the statement's entire execution happens inside the hook's stack
frame.  This extension installs `XClusterProcessUtility` as the hook; its
"after" code is in a `PG_FINALLY` block, so it runs even when the statement
fails with an error.

Two properties of the hook matter for what follows:

- It runs once per *statement*, not once per query string or per transaction.
  A query string or transaction containing five statements invokes the hook
  five times.
- It is re-entrant.  If a statement internally executes more SQL, each internal
  statement re-enters the full parse-then-execute pipeline, including the hook,
  nested inside the outer invocation.  The cases that matter here:
  - `CREATE/ALTER/DROP EXTENSION`: the extension's script runs its member DDLs
    nested under the extension statement.
  - `REFRESH MATERIALIZED VIEW CONCURRENTLY`: internally runs utility
    statements such as `CREATE TEMP TABLE` and `DROP TABLE`.
  - DDLs run from inside functions, procedures, and `DO` blocks.  Unlike the
    first two cases, these nested DDLs are real user DDLs and should be
    replicated.

Postgres distinguishes how a utility statement was reached via the hook's
`context` argument.  A *complete query* is a free-standing statement: sent by
the client, or run from a function or SPI (the Server Programming Interface,
Postgres's API for running SQL from C code).  A *subcommand* is a portion of
another statement that Postgres dispatches through ProcessUtility internally.
For example, `CREATE TABLE t (id serial)` is a single statement, but Postgres
implements the `serial` column by internally running a `CREATE SEQUENCE`
statement, dispatched as a subcommand.  (Most statements, e.g., a `CREATE
TABLE` without serial columns, have no subcommands.)  A subcommand has no
independent query text.  The extension only captures complete queries;
subcommands can be ignored as their effects will happen by simply rerunning
the complete query that encapsulates them.

### Event triggers

Unlike regular triggers (`CREATE TRIGGER`), which fire on data changes to a
particular table, event triggers (`CREATE EVENT TRIGGER`) fire on DDL commands,
database-wide.  Postgres fires them at fixed points during
`standard_ProcessUtility`'s execution of a DDL:

| Event | When it fires |
|---|---|
| `ddl_command_start` | Before the DDL executes |
| `ddl_command_end` | After the DDL executes (the transaction has not committed yet) |
| `sql_drop` | Just before `ddl_command_end`, if the DDL dropped objects |
| `table_rewrite` | When a DDL is about to rewrite a table's storage |

Inside `ddl_command_end` handlers, Postgres exposes what the DDL actually did
through support functions: `pg_event_trigger_ddl_commands()` returns one row
per executed command with the affected object's OID,
`pg_event_trigger_dropped_objects()` lists dropped objects, and
`pg_event_trigger_table_rewrite_oid()` identifies a rewritten table.  This
information (in particular the OIDs of newly created objects) exists only
after execution and is not available any other way.

Event triggers do not fire for statements on *global objects* (databases,
roles, and tablespaces) because those are shared across databases while event
triggers are per-database.

In regular Postgres they also do not fire for `TRUNCATE`; see
[TRUNCATE](#truncate) for the YugabyteDB change there.

## How the extension intercepts DDLs

Replicating a DDL requires two kinds of information that appear at different
times:

1. The exact SQL text to ship to the target, with correct statement
   boundaries.  This is available *before* execution, from the parse tree's
   `stmt_location`/`stmt_len`.  Event triggers cannot recover it: inside a
   trigger, only the whole query string is available, not the boundaries of the
   one statement being run.  Thus we need to rely on the ProcessUtility hook
   for this information.
2. What the DDL actually did: which objects it created, dropped, or rewrote,
   and their newly assigned OIDs.  This is available only *after* execution,
   through the event-trigger support functions.

So the extension uses both mechanisms, sharing state between them through
global variables in `yb_xcluster_ddl_replication.c`:

| | ProcessUtility hook | Event triggers |
|---|---|---|
| Fires on | Every utility statement, DDL or not, including ones event triggers cannot see (e.g., `CREATE DATABASE`) | Event-trigger-capable DDLs only |
| Information available | The raw parse tree and the exact statement text | The resolved effects: created/dropped/rewritten objects and their OIDs |
| Role | Save the statement text into globals; track which node is being processed; set the target-bypass default; clean all of it up when the statement ends | Decide whether and how to replicate; build the JSON payload; write to `ddl_queue` / `replicated_ddls` |

### The processing rule

Because statements can run other statements, one query executes a *tree* of
nodes: the client's statement is the root, and its children are the
subcommands and nested complete queries it runs.  At each node the extension
makes one binary choice:

- **Ignore this node** and keep looking at its children.  This is what happens
  for statements that are not event-trigger-capable DDLs (e.g., `CALL`, `DO`):
  the node itself is nothing to replicate, but a DDL nested inside it is.
- **Process this node** and then **ignore the rest of its subtree**.  Replaying
  the processed statement on the target reproduces everything beneath it, so
  the subtree must not be captured (it would double-execute) or clobber the
  captured text.
  - Note that "Process" doesn't necessarily mean we will replicate the
    statement.  We may choose to not replicate (temp table), in which case we
    still choose to ignore all its children.

### XClusterProcessUtility hook

Note that the hook is installed by `_PG_init` when the library is loaded, and
the library is in YugabyteDB's default `shared_preload_libraries`.  So the hook
runs in every backend of every database, even ones where the extension has not
been created; there it just captures and clears statement state without any
event triggers ever firing, and nothing is replicated.

The hook brackets every utility statement:

```
XClusterProcessUtility
  HandleQueryStart            -> set target-bypass default; save statement
  |                              text + parse tree into globals (unless inside
  |                              a processed node's subtree)
  standard_ProcessUtility     -> the DDL itself runs here;
  |                              event triggers also fire at this point
  PG_FINALLY:
    HandleQueryEnd            -> clear the captured state
```

`HandleQueryStart` saves the statement's text bounds (`query_string`,
`query_location`, `query_len`) and parse tree into global variables, unless a
node is already being processed (in that case this statement is part of its
subtree and must not overwrite the captured text).  `HandleQueryStart` also
sets `yb_xcluster_target_ddl_bypass` on the target side to handle replicated
DDLs there, and verifies statements run by extension scripts (see
[Extension DDLs](#extension-ddls)).

`HandleQueryEnd` clears all the state once the DDL finishes executing.  Because
the teardown is in `PG_FINALLY`, a failing DDL cannot leak captured state into
the next statement.

Statements run as subcommands are ignored entirely (no capture, no teardown).
We already capture and replicate the outer statement, which will run all of
these same subcommands on the target.

### The event triggers

The install script registers these triggers, all pointing at C functions in
this extension:

| Trigger event | C function | Fires for |
|---|---|---|
| `ddl_command_start` | `handle_ddl_start` | All event-trigger-capable DDLs, plus a second registration scoped `WHEN TAG IN ('TRUNCATE TABLE')` |
| `ddl_command_end` | `handle_ddl_end` | Same as above |
| `sql_drop` | `handle_sql_drop` | DDLs that dropped objects |
| `table_rewrite` | `handle_table_rewrite` | Table-rewriting DDLs |

- `handle_ddl_start` fetches the replication role and caches it in a global
  for the other entry points to reuse, marks this node as the one being
  processed (see [The processing rule](#the-processing-rule)), and resets the
  per-DDL state (e.g., information on tables that have been rewritten).

- `handle_sql_drop` and `handle_table_rewrite` collect information about drops
  and rewrites as they happen.

- `handle_ddl_end` makes the final replication decision and, if the DDL is to
  be replicated, writes to the extension tables.

Every entry point does nothing when the extension should not act on the
command (this check is done by `IsDisabled`):

- When the database is not in xCluster automatic mode (the replication role is
  neither `AUTOMATIC_SOURCE` nor `AUTOMATIC_TARGET`).
- When the DDL is operating on this extension itself (a `CREATE/ALTER/DROP
  EXTENSION` for `yb_xcluster_ddl_replication`).
- When the statement is nested under the node being processed (see
  [The processing rule](#the-processing-rule)).

## Source flow

For a DDL on the source (replication role `AUTOMATIC_SOURCE`):

```
XClusterProcessUtility
  HandleQueryStart      -> capture query_string / parsetree
  standard_ProcessUtility
    ddl_command_start   -> HandleSourceDDLStart
                            reset yb_should_replicate_ddl, clear rewrite list
    (the DDL executes)  -> DocDB schema changes happen here
      table_rewrite     -> HandleSourceTableRewrite fires during execution,
                            before a table is rewritten
    sql_drop            -> HandleSourceSQLDrop
    ddl_command_end     -> HandleSourceDDLEnd
                             decide replication; if replicating, build JSON
                             and INSERT into ddl_queue + replicated_ddls
  HandleQueryEnd        -> tear down captured state
```

The decision of whether to replicate is made at `ddl_command_end`.  For each
command, it decides by command tag whether that command makes the DDL
replicable, and records any extra information the target will need:

- Relation-creating commands (`CREATE TABLE / INDEX / TABLE AS / SELECT INTO /
  MATERIALIZED VIEW`) record the new relations' OID/colocation assignments.  The
  target uses these to create its relations with matching identifiers: xCluster
  replication connects DocDB tables on the two sides, so both sides must agree
  on which DocDB table backs which relation.
- Type, sequence, and `ALTER TABLE/INDEX` commands go through specialized
  handlers (enum-label maps, sequence maps, column-type-change checks, table
  rewrites).
- Tags in the pass-through allow-list are replicated as-is; tags in the ignore
  list are skipped.
- Anything else raises an error telling the user to use manual replication
  mode.

Only if at least one command is replicable does the captured query string get
inserted into `ddl_queue`.  A DDL may freely mix replicable commands with
ignorable ones: the whole query string is replayed on the target, which
reproduces both.

The exception is mixing temporary and permanent objects: temporary objects must
not be created on the target, but the query string can only be replayed whole,
so we reject these DDLs.

If `enable_manual_ddl_replication` is set, `HandleSourceDDLEnd` skips all
analysis and replicates the query verbatim with a `manual_replication` marker.

## Target flow

The extension does not block target DDLs itself.  The blocking is enforced in
the TServer (`pg_client_session.cc`), which refuses DDL writes against a
database whose role is `AUTOMATIC_TARGET` unless the session's
`yb_xcluster_target_ddl_bypass` flag is set.  The extension's job on the target
is to drive that flag, per statement:

- The hook defaults the flag to true for every statement, so statements that
  never reach an event trigger (e.g., `CREATE DATABASE`) are not blocked.
- The `ddl_command_start` trigger (`HandleTargetDDLStart`) sets it back to
  false for trigger-handled DDLs, then re-allows specific cases: DDLs replayed
  by the poller (marked by the `yb_xcluster_automatic_mode_target_ddl` session
  variable) and manual-replication-mode DDLs.
- DDLs on temporary relations are re-allowed separately, by the
  `RecordTempRelationDDL` callback, since temp relations are not replicated.

For a replayed DDL, `HandleTargetDDLEnd` records the DDL into
`replicated_ddls`, using the `ddl_queue_primary_key_*` session variables that
the poller set for the transaction to match the source-side `ddl_queue` row.

The function `yb_xcluster_handle_phantom_indexes` additionally sets the
`missing_ok` (i.e., `IF EXISTS`) option on index `DROP`/`RENAME`/`ALTER`
statements on the target, turning them into no-ops when the index does not
exist there.  This way replication does not halt on invalid ("phantom")
indexes that exist on the source but not the target.

If `enable_manual_ddl_replication` is set, the DDL is allowed through (see the
bypass cases above) and is not recorded into `replicated_ddls`.  Manual mode is
the escape hatch for DDLs the extension cannot replicate: the user runs the DDL
on the source and then again on the target, with this GUC set in both sessions;
the `manual_replication` marker on the source-side `ddl_queue` row tells the
ddl_queue handler not to re-execute it.

## Special DDL handling

### TRUNCATE

In standard Postgres, `TRUNCATE` does not fire
`ddl_command_start`/`ddl_command_end`.  Our YugabyteDB fork adds support for
it, but only for event triggers that explicitly name `TRUNCATE TABLE` in their
tag filter (so that pre-existing catch-all event triggers keep their Postgres
behavior).  The install script therefore registers a second pair of triggers
scoped `WHEN TAG IN ('TRUNCATE TABLE')`, routing to the same
`handle_ddl_start`/`handle_ddl_end` handlers.  Without these, a `TRUNCATE`
would never reach the source-capture path.

### REFRESH MATERIALIZED VIEW [CONCURRENTLY]

`REFRESH MATERIALIZED VIEW CONCURRENTLY` runs nested utility statements: while
it executes, it builds SQL strings like `CREATE TEMP TABLE ...`,
`DROP TABLE ...` and runs them through SPI.  Each of those statements re-enters
the parser and `XClusterProcessUtility` like any client statement.  Without
special handling, each nested statement would overwrite and then clear the
captured query string before the outer `REFRESH`'s `ddl_command_end` fires
(recording an empty query) and would also fire its own event triggers,
incorrectly trying to replicate the internal temp-table DDLs.

This is handled entirely by [the processing rule](#the-processing-rule): the
`REFRESH` is processed at its `ddl_command_start`, so its nested statements
are part of its subtree and are ignored: they neither capture query text nor
fire the extension's trigger logic.  The `REFRESH`'s own `ddl_command_end`
still replicates the statement normally.  This also covers the edge case of a
`REFRESH` nested inside another `REFRESH` (e.g., run from a function in the
matview query): the inner one is part of the outer's subtree, so it is ignored.

### Extension DDLs

A `CREATE/ALTER/DROP EXTENSION` statement (for any extension) is replicated as
a single top-level command.  It is processed at its `ddl_command_start`, so
per [the processing rule](#the-processing-rule) the member-object DDLs that
its extension script runs underneath it are ignored, since replaying the
top-level statement on the target reruns the script there.

The script's DDLs are still verified at their `ddl_command_start`, even though
they are otherwise ignored. Extensions whose scripts contain complex DDLs such
as `CREATE TABLE` are rejected and must be created before the database is
added to xCluster.

DDLs operating on this extension itself are ignored entirely.  This extension
is installed on both sides as part of xCluster setup, so we don't need to
capture or replicate it.

### Table rewrite (ALTER TABLE)

Some `ALTER TABLE` forms (e.g., `ALTER COLUMN TYPE`) rewrite the table rather
than just updating the catalog: the old DocDB table is discarded, a new one is
created, and the relation's `relfile_oid` is repointed at it.  Dependent
indexes are rewritten the same way.  The target needs to know about these new
table/index OID assignments, since it needs to create new pollers for these
new DocDB tables.

The `table_rewrite` trigger fires once per table about to be rewritten, and
`HandleSourceTableRewrite` collects each such table's OID into a global list
(cleared at `ddl_command_start`).  At `ddl_command_end`, an `ALTER TABLE` whose
OID is in that list is then treated like a newly created relation: its new
relfile assignments are recorded into the `ddl_queue` JSON, along with those of
its indexes.

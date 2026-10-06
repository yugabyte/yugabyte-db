# AGENTS.md

This document provides a guide for agents working on YugabyteDB

### Deploying and running

For agents that want to deploy, configure and run YugabyteDB refer to instructions at ./docs/content/stable/quick-start

### Repo Structure

| Directory | What it contains |
|---|---|
| `src/` | Core database code: PostgreSQL fork (`src/postgres/`), YugabyteDB C++ storage engine (`src/yb/`), Odyssey connection pooler (`src/odyssey/`) |
| `java/` | Java client library, CDC connector, and DB tests |
| `managed/` | YugabyteDB Anywhere (YBA) platform — orchestration UI, CLI, node agent, and backend (Scala/Java) |
| `docs/` | Source files for the docs website (docs.yugabyte.com) |
| `python/` | Python build utilities and test infrastructure scripts |
| `build-support/` | Build system scripts, linting, and third-party dependency tooling |
| `cmake_modules/` | CMake modules for locating dependencies and custom build functions |
| `cloud/` | Docker, Kubernetes, and Grafana deployment configurations |
| `yugabyted-ui/` | Yugabyted web UI (React frontend + Go API server) |
| `architecture/` | Internal design documents and architecture specs |
| `troubleshoot/` | Troubleshooting framework backend and UI |

### Coding and Development

When working on DB code (`src/`), refer to `src/AGENTS.md` for build and test guidance

### Metric naming gate (src/ and managed/)

Applies whenever you add, rename, or restructure a metric: C++ `METRIC_DEFINE_*` / prototypes in `src/`, YBA `PlatformMetrics` / Prometheus collectors in `managed/`, and any codegen that emits metric names (e.g. `gen_yrpc`). Full guide with examples: `src/yb/util/METRIC_NAMING.md`.

**A metric name says what is measured (+ unit). Any part of the name that is a *value* -- an RPC method, service, statement type, task type, thread pool, table, command, server type, enum number -- goes in a label.**

Before you add a metric, apply the one-question test to every token after the base name:

> Does this token name a value of a shared dimension (-> label), or a different quantity (-> separate metric)?

- `threads_started_acceptor` vs `threads_started_rpc_worker`: same quantity, different pool -> label `thread_pool`.
- `ybp_health_check_master_down` vs `ybp_health_check_tserver_down`: same check, different server -> label `server_type`.
- `rocksdb_block_cache_add` vs `rocksdb_block_cache_add_failures`: different quantities -> separate metrics (correct as-is).

Rules (IDs are shared with `REVIEW.md`):

- **MN1 -- Static names only.** The name is a string literal. Do not build it with `Format()`, `+`, `StrCat`, `BOOST_PP_CAT`/token pasting, `Owning*Prototype(prefix + "_" + value)`, or codegen/macros that stamp one `METRIC_DEFINE` per method, command, statement, or pool.
- **MN2 -- No values in names.** Never put enum numbers, task types, mem-tracker paths, service/method names, table/tablet IDs, or server types in a name.
- **MN3 -- Labels via `MetricEntity`.** No new metrics framework is needed: define a `MetricEntity` type for the dimension, put the value in its attributes, and instantiate the *same* prototype on one entity per value. A new entity type also needs a branch in `MetricEntity::ReconstructPrometheusAttributesUnlocked` (`src/yb/util/metric_entity.cc`) that exports its attributes as labels; without one, the Prometheus writer drops its metrics and they never reach `/prometheus-metrics`. Copy existing precedents: table/tablet entities (`rocksdb_*`), `METRIC_ENTITY_cgroup` in `tserver_cgroup_manager.cc` (with its `cgroup` branch in `metric_entity.cc`), YSQL catalog-cache `db_oid`/`table_name` labels.
- **MN4 -- YBA: one enum constant per quantity.** `PlatformMetrics` derives the name from the constant, so do not add one constant per server type, check, or task. Add one constant and attach the varying value as a label.
- **MN5 -- Honest prefix.** Counts (`NumRetriesToExecute`, `CatalogCacheMisses`) do not go under `handler_latency_`. The prefix and unit must match the metric type.
- **MN6 -- Cardinality.** Bounded values (method, pool, op, check) -> label. Unbounded values (table, tablet, user, query) -> label on the right entity, and justify in the PR/diff summary why the metric needs that dimension at all.
- **MN7 -- No hand-added suffixes.** Do not add `_sum`, `_count`, or `_total` yourself. YB histograms already emit `_sum`/`_count` and quantiles as a `quantile` label; some pipelines append `_total`.
- **MN8 -- Renames are breaking.** Renaming or moving a metric (including moving an RPC to another service, which renames its generated metric, or refactoring a mem-tracker hierarchy) breaks YBA, YBM, and Perf Advisor dashboards and alerts. Call it out in the diff summary with the old -> new name and the consumers you checked. Prefer folding a name-encoded family into labels over another rename.

Before you finish a change that touches metrics:

1. List every metric name you added or changed in the diff summary.
2. Confirm each one passes the one-question test and MN1-MN8.
3. If `metric-name-lint` is installed, run `python3 build-support/metric_name_lint.py --files <changed files>`. Fix findings. Do not add a baseline entry or a `metric-name-lint: allow(...)` suppression unless the user asks for one; if they do, the suppression reason must name the migration issue.
4. Existing name-encoded families (`handler_latency_yb_*`, `rpcs_in_queue_*`, `threads_started_*`, `<Task>_Task`/`_Attempt`, `mem_tracker_*` paths, `ybp_health_check_*`) are known debt. Do not extend them with new members. If you must touch one, ask the user whether to migrate it to labels in the same change or a follow-up.

## Cursor Cloud specific instructions

### Environment

The VM image is `yugabyteci/yb_build_infra_almalinux9_x86_64` which ships with: Clang, JDK 17, Go, Python 3.11, CMake, Ninja, SBT, Node.js 22, Rust, and all C++ build dependencies.

### Building from source

```bash
./yb_build.sh release daemons initdb --sj --skip-pg-parquet --no-odyssey --no-ybc
```

This is the Cloud environment `install` command. Builds snapshot `build/latest` (yb-master, yb-tserver, postgres, initdb). Agents booting from a successful Build already have those binaries; rerun only if the branch changes build inputs. Do not start yugabyted in `install`; Builds keep files, not processes.

Flags: `--sj` skips Java, `--skip-pg-parquet` skips the parquet extension, `--no-odyssey` skips the connection pooler, `--no-ybc` skips the backup controller. Add targets back as needed. First compile is ~15-20 minutes on 8 vCPUs; later Builds reuse `/opt/yb-build` and incremental objects.

### Running the database

If `build/latest` is present, start the cluster. Otherwise compile first.

```bash
python3 bin/yugabyted start --advertise_address 127.0.0.1 --base_dir /tmp/yb-data
```

For a multi-node cluster, start additional nodes with `--join <first-node-address>` and a distinct `--advertise_address` and `--base_dir` each.

Ports: YSQL on 5433, YCQL on 9042, yb-master UI on 7000, yb-tserver UI on 9000.

Connect: `build/latest/postgres/bin/ysqlsh -U yugabyte -d yugabyte -h 127.0.0.1`

### Yugabyted UI

`yugabyted-ui/ui/`: `npm ci` (Node.js >= 22.18), `npm start` on port 3000, `npm run typecheck`.

### Prose discipline — write for the reader, not for volume

AI-written text runs long: PR and diff descriptions that narrate the diff, comments that
restate the line below them, design docs padded with editorial framing. It reads as
thorough, but it costs review time, and every extra sentence is one more claim that has to
stay true as the code moves.

There are no hard limits here. Judge each piece by what a reader who already knows this
repo needs:

- **Code comments** — don't restate the code. Comment the *why* when it isn't obvious: an
  invariant, a cross-component contract, a locking or ordering constraint, a workaround and
  the upstream bug behind it. Not a label for the block below it, not narration of the
  change you just made.
- **PR / Phorge diff descriptions** — the motivation (a reviewer who doesn't know why is
  the expensive case), what changed, and whatever the reader must *act* on: new gflags,
  upgrade/rollback consequences, migration steps. Not a narration of the diff. The test
  plan is a separate section — keep it to what was run.
- **Design docs (`architecture/`) and agent docs (`AGENTS.md`, `.claude/skills/`)** — how
  the system is wired today, why it's built that way, and what the reader has to do. Cut
  restated context and the history of what the code used to do.
- **Commit messages** — subject, plus the why when the why isn't obvious.

The test for a sentence: would a reader who knows this repo do or believe anything
differently without it? If not, cut it.

**Motivation is not filler** — why a design is the way it is is exactly what a reader can't
recover from the code; what's being cut is text that says the same thing twice, not text
that explains. And don't over-correct: the goal is *fewer, load-bearing* words, not
stripped docs. Deleting a section that describes live behavior is a worse outcome than
leaving it wordy, and this applies to your own diff, not a cleanup tour of files you didn't
touch.

This does **not** apply to the user-facing docs website (`docs/`), which follows its own
editorial style guide and is written for readers who do *not* know this repo.

Trim before you publish, not in review: the `create-pr` and `create-diff` skills re-check
this as a step.

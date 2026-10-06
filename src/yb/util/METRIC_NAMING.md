# Metric naming standard

**A metric name says what is measured. Anything that varies goes in a label.**

If part of a metric name is a *value* (an RPC method, statement type, thread pool, task type,
table, command, enum number, server type...), it belongs in a label.

```
BAD:  service_request_bytes_yb_tserver_TabletServerService_Write
GOOD: service_request_bytes{server_type="yb_tserver", service_type="TabletServerService", service_method="Write"}
```

## The one-question test

> Does this token name **a value of a shared dimension** (-> label) or **a different quantity**
> (-> a separate metric)?

| Names | Verdict |
|---|---|
| `threads_started_acceptor` vs `threads_started_rpc_worker` | same quantity, different pool -> **label** |
| `ybp_health_check_master_down` vs `ybp_health_check_tserver_down` | same check, different server -> **label** |
| `rocksdb_block_cache_add` vs `rocksdb_block_cache_add_failures` | different quantities -> **separate metrics** |

## Why

- **Name-keyed backends pay per name.** OpenObserve keeps one stream per metric name with hourly
  partitions. Collapsing a few name-encoded families in a test environment took 3,023 -> 1,458
  streams.
- **Renames break consumers.** Perf Advisor lists three historical names for one memtable gauge
  because each mem-tracker refactor renamed the metric. Moving an RPC between services renames
  its metric too.
- **Every consumer ends up regex-parsing `__name__`**, e.g. YBA's
  `handler_latency_(yb_[^_]*)_([^_]*)_([^_]*)(.*)`, which mis-parses `yb_stateful_service`.
- **You can't aggregate across the dimension** (`sum by (service_method)`) without that regex.

## Rules

1. Name = what is measured (+ unit). Nothing that varies at runtime or per call site.
2. Don't build names from variables: no `Format()`/`+`/`Sprintf`/f-strings, no token pasting,
   no code generation that stamps one metric per method/command/statement.
3. Never put enum numbers, task types, tracker paths or IDs in a name.
4. Keep the prefix honest: a count doesn't belong under `handler_latency_`.
5. Bounded values (method, pool, op, check) -> label. Unbounded values (table, tablet, user,
   query) -> label on the right entity, and ask whether you need it at all.
6. Don't hand-add `_sum`/`_count`/`_total`. YB histograms emit `_sum`/`_count`; some pipelines
   append `_total`.

## How to do it right

**C++ (yugabyte-db).** Labels come from `MetricEntity` attributes. Define an entity type for the
dimension and instantiate the same prototype on one entity per value. Precedents:
`METRIC_ENTITY_cgroup` (`tserver_cgroup_manager.cc`) and table/tablet entities.

```cpp
METRIC_DEFINE_entity(thread_pool);   // + an allowlist branch in MetricEntity's Prometheus attributes
METRIC_DEFINE_gauge_uint64(thread_pool, threads_started_by_pool, "Threads started", ...);

auto e = METRIC_ENTITY_thread_pool.Instantiate(registry, "thread_pool." + pool,
                                               {{"thread_pool", pool}});
METRIC_threads_started_by_pool.InstantiateFunctionGauge(e, ...);
```

**Go (client_golang).** Use `Namespace`/`Subsystem` for prefixes and `*Vec` for values.

```go
BAD:  prometheus.GaugeOpts{Name: fmt.Sprintf("%s_queue_size", pool)}
GOOD: prometheus.NewGaugeVec(prometheus.GaugeOpts{Namespace: "billing", Name: "queue_size"}, []string{"pool"})
```

**Java (io.prometheus / Micrometer).**

```java
BAD:  Gauge.builder().name("ts_universe_" + type + "_status")
GOOD: Gauge.builder().name("ts_universe_status").labelNames("universe_type")
```

**Python (prometheus_client).**

```python
BAD:  Gauge(f"ybm_{job}_duration_seconds", "...")
GOOD: Gauge("ybm_job_duration_seconds", "...", labelnames=["job"])
```

**Queries.** Select on labels, not on `__name__` regexes:
`rpcs_in_queue{service_type=~"Master.*"}`, not `{__name__=~"rpcs_in_queue_yb_master_.*"}`.

## The CI gate (`metric-name-lint`)

- **New violations fail the PR.** Existing debt is recorded in the baseline file and only
  reported.
- **The baseline can only shrink.**
  - Fixed an old one? CI fails until you run `metric_name_lint.py --update-baseline` and commit
    the smaller baseline.
  - A PR that *adds* baseline entries fails unless it carries the `metric-naming-exception`
    label, which needs observability-owner approval (CODEOWNERS on the baseline file).
- **One-off exception:** add
  `// metric-name-lint: allow(<why, and the migration issue>)` on the line or the line above.
  A reason is required.
- **Queries that regex-match `__name__`** are reported as warnings, not failures.
- **Run it locally** with `python3 <path>/metric_name_lint.py`, or add `--files <changed files>`
  to check only those files.

The linter checks source statically. yugabyte-db also runs a nightly check of a live
`/prometheus-metrics` scrape (`--scrape`), which catches names assembled in ways the static
rules can't see.

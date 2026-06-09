---
title: YSQL distributed tracing
linkTitle: YSQL distributed tracing
headerTitle: YSQL distributed tracing
description: Export OpenTelemetry traces for YSQL query execution.
headcontent: Trace YSQL queries with OpenTelemetry
menu:
  stable:
    parent: monitor-and-alert
    identifier: ysql-distributed-tracing
    weight: 130
type: docs
tags:
  feature: tech-preview
rightNav:
  hideH4: true
---

When a YSQL query is slow, the time can be spent in many places: parsing, planning, execution, transaction commit, and RPC calls to tablet servers. YugabyteDB can export timing data for these stages as **OpenTelemetry (OTel)** traces so you can inspect a waterfall view of query execution in tools such as Jaeger, Grafana Tempo, or Honeycomb.

Distributed tracing is {{<tags/feature/tp>}} and currently instruments the **YSQL (Postgres) layer only**. Tracing inside tablet servers is not included in this release.

## How it works

YSQL distributed tracing follows the [W3C Trace Context](https://www.w3.org/TR/trace-context/) standard. Your application (or a SQL comment or session setting) supplies a `traceparent` value. YugabyteDB creates spans for the query lifecycle and exports them to an OTel collector over OTLP/HTTP.

Each traced query produces a **trace** made up of **spans**. Spans are nested to show where time is spent—for example, planning, execution, commit, and individual RPC calls to `PgClientService`.

When tracing is disabled (the default), there is no measurable performance impact. When tracing is enabled for a query, other queries and other YSQL backends are not affected.

## Prerequisites

- YSQL must be enabled on the cluster.
- An OTLP/HTTP endpoint must be reachable from each YB-TServer node. YugabyteDB does not export traces unless `otel_collector_traces_endpoint` is set.
- Because distributed tracing is a preview feature, add `otel_collector_traces_endpoint` to the [allowed_preview_flags_csv](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list before setting it.

## Set up tracing

The following example uses [Jaeger](https://www.jaegertracing.io/) as the trace backend. Jaeger accepts OTLP over HTTP on port 4318.

### 1. Start Jaeger

Run Jaeger all-in-one with OTLP enabled:

```sh
docker run --rm --name jaeger \
  -e COLLECTOR_OTLP_ENABLED=true \
  -p 16686:16686 \
  -p 4318:4318 \
  jaegertracing/all-in-one:1.61
```

Open the Jaeger UI at [http://localhost:16686](http://localhost:16686).

### 2. Configure YugabyteDB

Set the following YB-TServer flags on each node in the cluster. Changing these flags requires a YB-TServer restart.

| Flag | Description |
| :--- | :---------- |
| [allowed_preview_flags_csv](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) | Must include `otel_collector_traces_endpoint`. |
| otel_collector_traces_endpoint | OTLP/HTTP URL where spans are exported—for example, `http://<collector-host>:4318/v1/traces`. Setting this flag enables tracing infrastructure in each YSQL backend process. |
| otel_batch_max_queue_size | Maximum spans buffered before export. Spans beyond this limit are dropped. Default: `2048`. |
| otel_batch_schedule_delay_ms | Milliseconds between batch exports. Default: `5000`. Lower values reduce export latency but increase export frequency. |
| otel_batch_max_export_batch_size | Maximum spans per export batch. Default: `512`. |

For a local single-node cluster started with yugabyted:

```sh
./bin/yugabyted start \
  --tserver_flags "allowed_preview_flags_csv=otel_collector_traces_endpoint,otel_collector_traces_endpoint=http://127.0.0.1:4318/v1/traces"
```

If you use YugabyteDB Anywhere, set the flags using [Edit configuration flags](../../../yugabyte-platform/manage-deployments/edit-config-flags/#modify-configuration-flags).

{{< note title="Note" >}}

If `otel_collector_traces_endpoint` is not set, attempting to use the `yb_dist_tracecontext` configuration parameter returns an error indicating that distributed tracing is not enabled.

{{</note >}}

### 3. Trace a query

After the cluster is running with tracing configured, enable tracing for individual queries using one of the following methods.

#### SQL comment (per query)

Prepend or append a block comment that contains a W3C `traceparent` value. The comment must be the **first** block comment at the start of the query, or the **last** block comment at the end of the query.

```sql
/*traceparent='00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01'*/
SELECT * FROM orders WHERE customer_id = 500;
```

You can also place the comment at the end of the statement:

```sql
SELECT * FROM orders WHERE customer_id = 500
/*traceparent='00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01'*/;
```

If another block comment appears before a leading `traceparent` comment, or after a trailing `traceparent` comment, the `traceparent` is not parsed.

#### Configuration parameter (per session or transaction)

Set the `yb_dist_tracecontext` parameter to trace every query in the session or transaction:

```sql
BEGIN;
SET LOCAL yb_dist_tracecontext = 'traceparent=''00-00000000000000000000000000000001-0000000000000005-01''';
SELECT * FROM users WHERE id = 10;
UPDATE accounts SET balance = balance - 100 WHERE id = 10;
COMMIT;
```

If both the parameter and a SQL comment supply a `traceparent`, the parameter takes priority and a warning is emitted.

To stop tracing for the session:

```sql
RESET yb_dist_tracecontext;
```

### 4. View traces

Run a traced query, wait a few seconds for spans to be batched and exported (controlled by `otel_batch_schedule_delay_ms`), then open the Jaeger UI.

1. Select **Service** `ysql`.
2. Click **Find Traces**.
3. Open a trace to see spans such as `query`, `parse`, `plan`, `execute`, `commit`, and RPC spans.

## Trace data

### Process attributes

Each YSQL backend exports process metadata with every trace:

| Attribute | Description |
| :-------- | :---------- |
| service.name | Always `ysql`. |
| service.instance.id | UUID of the YB-TServer node running the YSQL backend. |
| process.pid | Operating-system PID of the YSQL backend process. |

### Span attributes

Every span includes standard OpenTelemetry fields such as operation name, span ID, parent span ID, trace ID, start time, duration, span kind, and status.

Additional attributes depend on the span type:

| Span | Additional attributes |
| :--- | :-------------------- |
| Root (`query`) | `query.text` (truncated to 256 characters), `user.id` (PostgreSQL role OID) |
| RPC (for example, `rpc yb.tserver.PgClientService.Perform`) | `rpc.table_names` — tables accessed by the RPC |

Typical span names include:

- Query lifecycle: `query`, `parse`, `rewrite`, `plan`, `execute`, `commit`, `abort`
- Extended query protocol: `ext.parse`, `ext.bind`, `ext.execute`, `ext.sync`, `ext.describe`, `ext.flush`
- RPC calls: `rpc yb.tserver.PgClientService.Perform` and related RPC operation names

## Join application traces

Because YugabyteDB accepts W3C `traceparent` values, you can continue a trace started in application code. Pass the same `traceparent` in a SQL comment or `yb_dist_tracecontext` parameter so database spans appear as children of your application's root span in your observability backend.

## Related topics

- [Monitor with Active Session History](active-session-history-monitor/) — sample-based view of database wait events
- [Query tuning](query-tuning/) — optimize query performance with EXPLAIN, pg_stat_statements, and related tools
- [Jaeger integration](../../../integrations/jaeger/) — use YCQL as Jaeger trace storage (separate from YSQL query tracing)

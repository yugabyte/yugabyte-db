---
title: CDC using Apache Flink CDC
headerTitle: CDC using Apache Flink CDC
linkTitle: Flink CDC
description: Capture and stream YugabyteDB changes using Apache Flink CDC.
headcontent: Capture and stream YugabyteDB changes using Apache Flink CDC
cascade:
  tags:
    feature: tech-preview
menu:
  stable:
    identifier: explore-change-data-capture-flink-cdc
    parent: explore-change-data-capture
    weight: 300
type: indexpage
showRightNav: true
---

## Overview

[Flink CDC](https://nightlies.apache.org/flink/flink-cdc-docs-stable/) is a data integration framework built on [Apache Flink](https://flink.apache.org/) that captures row-level changes from operational databases and continuously applies them to downstream systems. Using Flink CDC with YugabyteDB as a source database enables you to stream change events for the following use cases:

- Real-time data propagation: Stream YugabyteDB changes to Kafka, Elasticsearch, Iceberg, or data lakes.
- Operational analytics: Feed a warehouse or lakehouse continuously.
- Event-driven architectures: Turn row-level changes into Flink streams.
- Near-zero downtime migrations: Combine snapshot and change capture to migrate away from YugabyteDB with minimal disruption.

Flink CDC uses the Debezium PostgreSQL connector internally to capture row-level changes from YugabyteDB's logical replication stream. The key difference from a standalone Debezium connector is that Flink CDC converts change records into a continuous Flink stream, allowing you to process data using the Flink Table API or Flink SQL before routing it to any downstream Flink integration (for example, Kafka, Iceberg, or a JDBC sink).

The Yugabyte fork of Flink CDC is maintained at [github.com/yugabyte/flink-cdc](https://github.com/yugabyte/flink-cdc). Pre-built connector JARs are published on the repository's [Releases page](https://github.com/yugabyte/flink-cdc/releases).

## Get started

Deploy a YugabyteDB-to-PostgreSQL pipeline using Docker Compose and the Flink SQL Client.

{{<lead link="./get-started/">}}
[Get started](./get-started/) with Flink CDC on YugabyteDB.
{{</lead>}}

## Architecture

The Flink CDC integration involves a three-stage streaming pipeline:

1. **Change capture (source)** — The Flink `postgres-cdc` connector reads raw change records from YugabyteDB via a logical replication slot using the `pgoutput` decoding plugin.
1. **Stream processing (Flink)** — The connector converts the incoming change records (Debezium format internally) into an unbounded Flink dynamic table. This stream can be processed, filtered, or transformed using Flink SQL or the DataStream API.
1. **Data sink (destination)** — The processed Flink stream is written continuously to a downstream system using a Flink sink connector (for example, a JDBC sink, Kafka topic, or Iceberg table).

## Best practices

- Always define primary keys on source tables; choose distributed keys to avoid write hotspots.
- Use a unique `slot.name` value for every pipeline to prevent conflicts on active replication slots.
- Set `decoding.plugin.name` to `pgoutput`. YugabyteDB does not support the default `decoderbufs` plugin.
- Do **not** enable `scan.incremental.snapshot.enabled` — incremental snapshots are not supported with YugabyteDB.
- Tune YugabyteDB CDC WAL retention flags (`cdc_wal_retention_time_secs`, `cdc_intent_retention_ms`) to exceed the maximum expected consumer downtime.
- Configure checkpointing together with a forgiving `fixed-delay` restart strategy (see [Unsupported scenarios](#unsupported-scenarios)).
- Monitor CDC lag, retention headroom, and checkpoint health as SLO signals.

## Unsupported scenarios

| Scenario | Details |
| :--- | :--- |
| Incremental snapshots | YugabyteDB does not currently support incremental snapshots. Keep `scan.incremental.snapshot.enabled` at its default `false`. Because checkpointing is unavailable during initial snapshots, long-running snapshot processes may encounter timeouts. Set `execution.checkpointing.interval` to `10min`, `execution.checkpointing.tolerable-failed-checkpoints` to `100`, and use `restart-strategy: fixed-delay` with `restart-strategy.fixed-delay.attempts: 2147483647`. |
| Transactional atomicity | Default configurations do not guarantee cross-table consistency during end-to-end propagation. |
| Schema evolution | DDL changes are not mirrored automatically and must be managed through manual intervention. |
| Exactly-once processing | The `postgres-cdc` source supports exactly-once, but end-to-end delivery depends on a transactional sink. Standard JDBC sinks provide at-least-once delivery. |
| PK-less tables | Tables without primary keys are not recommended. The common workaround using `scan.incremental.snapshot.chunk.key-column` relies on unsupported incremental snapshotting. |
| Shared replication slots | A replication slot must be consumed by at most one Flink pipeline at a time. Assign a unique `slot.name` to every pipeline. |

## Related articles

- [Flink CDC `postgres-cdc` connector (v3.5)](https://nightlies.apache.org/flink/flink-cdc-docs-release-3.5/docs/connectors/flink-sources/postgres-cdc/)
- [Flink CDC on GitHub](https://github.com/apache/flink-cdc)
- [Yugabyte fork of Flink CDC](https://github.com/yugabyte/flink-cdc)
- [CDC using PostgreSQL Replication Protocol](../using-logical-replication/)
- [YugabyteDB CDC architecture](../../../architecture/docdb-replication/change-data-capture/)

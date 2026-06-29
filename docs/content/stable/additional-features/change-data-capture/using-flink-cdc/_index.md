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

[Flink CDC](https://nightlies.apache.org/flink/flink-cdc-docs-stable/) is a data integration framework that combines [Apache Flink](https://flink.apache.org/) stream processing with [Debezium](https://debezium.io/) change capture to read row-level changes from operational databases and continuously apply them to downstream systems. Using Flink CDC with YugabyteDB as a source database enables you to stream change events for the following use cases:

- Real-time data propagation: Stream YugabyteDB changes to Kafka, Elasticsearch, Iceberg, or data lakes.
- Operational analytics: Feed a warehouse or lakehouse continuously.
- Event-driven architectures: Turn row-level changes into Flink streams.
- Near-zero downtime migrations: Combine snapshot and change capture to migrate away from YugabyteDB with minimal disruption.

Flink CDC uses the Debezium PostgreSQL connector internally to capture row-level changes from YugabyteDB's logical replication stream. The key difference from a standalone Debezium connector is that Flink CDC converts change records into a continuous Flink stream, allowing you to process data using the Flink Table API or Flink SQL before routing it to any downstream Flink integration (for example, Kafka, Iceberg, or a JDBC sink).

Yugabyte maintains a [Flink CDC build for YugabyteDB](https://github.com/yugabyte/flink-cdc) as a GitHub repository with pre-built connector JARs published on the [Releases page](https://github.com/yugabyte/flink-cdc/releases).

## Architecture

The Flink CDC integration involves a three-stage streaming pipeline:

1. Change capture (source): The Flink `postgres-cdc` connector reads raw change records from YugabyteDB via a logical replication slot using the `pgoutput` decoding plugin.
1. Stream processing (Flink): The connector converts the incoming change records (Debezium format internally) into an unbounded Flink dynamic table. This stream can be processed, filtered, or transformed using Flink SQL or the DataStream API.
1. Data sink (destination): The processed Flink stream is written continuously to a downstream system using a Flink sink connector (for example, a JDBC sink, Kafka topic, or Iceberg table).

![Flink architecture](/images/architecture/flink-architecture.png/)

## Use Flink CDC

Flink CDC with YugabyteDB is {{<tags/feature/tp idea="2658">}}. At a high level, a YugabyteDB-to-downstream Flink CDC pipeline looks like this:

1. Start a YugabyteDB cluster and note the IP address of a YB-TServer node that Flink can reach.
1. Create source tables in YugabyteDB, and run the following in `ysqlsh` to create the publication and logical replication slot used by the Flink `postgres-cdc` connector:

    ```sql
    CREATE PUBLICATION dbz_publication FOR ALL TABLES;
    SELECT * FROM pg_create_logical_replication_slot('flink', 'pgoutput');
    ```

    Use a unique slot name for each Flink pipeline, and tune CDC WAL retention so the slot can survive expected consumer downtime (see [Best practices](#best-practices)).
1. Deploy a Flink cluster with the `postgres-cdc` and JDBC connector JARs (see [Get started](./get-started/)).
1. Open the Flink SQL Client and define a `postgres-cdc` source table and a sink table.
1. Submit the Flink job: Define source and sink tables in the Flink SQL Client and run a streaming `INSERT INTO … SELECT …` job (see [Initiate the streaming job](./get-started/#initiate-the-streaming-job)).
1. Validate that INSERT, UPDATE, and DELETE operations propagate end-to-end, and monitor the job at the Flink Web UI (for example, `http://localhost:8081`).

To disable the feature, you have to cancel the Flink job. Optionally, drop the publication and replication slot when you no longer need change capture on the database. See [Disable the pipeline](using-flink-cdc/get-started/#disable-the-pipeline).

## Get started

Deploy a YugabyteDB-to-PostgreSQL pipeline using Docker Compose and the Flink SQL Client.

{{<lead link="./get-started/">}}
[Get started](./get-started/) with Flink CDC on YugabyteDB.
{{</lead>}}

## Best practices

- Always define primary keys on source tables; choose distributed keys to avoid write hotspots.
- Use a unique `slot.name` value for every pipeline to prevent conflicts on active replication slots.
- Set `decoding.plugin.name` to `pgoutput`. YugabyteDB does not support the default `decoderbufs` plugin.
- Do **not** enable `scan.incremental.snapshot.enabled`. YugabyteDB does not support Incremental snapshots.
- Tune YugabyteDB CDC WAL retention flags (`cdc_wal_retention_time_secs`, `cdc_intent_retention_ms`) to exceed the maximum expected consumer downtime.
- Configure checkpointing together with a forgiving `fixed-delay` restart strategy (see [Unsupported scenarios](#unsupported-scenarios)).
- Monitor CDC lag, retention headroom, and checkpoint health as SLO signals.

## Unsupported scenarios

- Incremental snapshots. YugabyteDB does not currently support incremental snapshots. Keep `scan.incremental.snapshot.enabled` at its default `false` setting. Because checkpointing is unavailable during initial snapshots, long-running snapshot processes may encounter timeouts. Recommended mitigation parameters include:
  - Set `execution.checkpointing.interval` to `10min`
  - Set`execution.checkpointing.tolerable-failed-checkpoints` to `100`
  - Use `restart-strategy: fixed-delay` with `restart-strategy.fixed-delay.attempts: 2147483647`.
- Transactional atomicity. Default configurations do not guarantee cross-table consistency during end-to-end propagation.
- Schema evolution. DDL changes are not mirrored automatically and must be managed through manual intervention.
- Exactly-once processing. The `postgres-cdc` source supports exactly-once, but end-to-end delivery depends on a transactional sink. Standard JDBC sinks provide at-least-once delivery.
- Primary key requirement. Tables without primary keys are not recommended. The common workaround using `scan.incremental.snapshot.chunk.key-column` relies on unsupported incremental snapshotting.
- Slot name conflicts. Assign a unique `slot.name` to every pipeline to prevent errors regarding active PIDs on the same slot.

## Related articles

- [Flink CDC postgres-cdc connector (v3.5)](https://nightlies.apache.org/flink/flink-cdc-docs-release-3.5/docs/connectors/flink-sources/postgres-cdc/)
- [Flink CDC](https://github.com/apache/flink-cdc)
- [CDC using PostgreSQL Replication Protocol](../../../architecture/docdb-replication/cdc-logical-replication/)
- [CDC using gRPC protocol](../../../architecture/docdb-replication/change-data-capture/)

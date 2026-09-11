---
title: CDC using PostgreSQL replication protocol
headerTitle: CDC using PostgreSQL replication protocol
linkTitle: PostgreSQL protocol
description: CDC using YugabyteDB PostgreSQL replication protocol.
headcontent: Capture changes made to data in the database
aliases:
  - /stable/explore/change-data-capture/using-logical-replication/
cascade:
  tags:
    feature: early-access
menu:
  stable:
    identifier: explore-change-data-capture-logical-replication
    parent: explore-change-data-capture
    weight: 240
type: indexpage
rightNav:
  hideH3: true
  hideH4: true
---

## Overview

YugabyteDB CDC captures changes made to data in the database and streams those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB database to downstream consumers based on its Write-Ahead Log (WAL). YugabyteDB CDC captures row-level changes resulting from INSERT, UPDATE, and DELETE operations in the configured database and publishes it further to be consumed by downstream applications.

### Highlights

#### Resilience

YugabyteDB CDC with PostgreSQL Logical Replication provides resilience as follows:

1. Following a failure of the application, server, or network, the replication can continue from any of the available server nodes.

2. Replication continues from the transaction immediately after the transaction that was last acknowledged by the application. No transactions are missed by the application.

#### Security

Because YugabyteDB is using the PostgreSQL Logical Replication model, the following applies:

- The CDC user persona will be a PostgreSQL replication client.

- A standard replication connection is used for consumption, and all the server-side configurations for authentication, authorizations, SSL modes, and connection load balancing can be leveraged automatically.

#### Guarantees

CDC in YugabyteDB provides the following guarantees.

| GUARANTEE | DESCRIPTION |
| :----- | :----- |
| Per-slot ordered delivery guarantee | Changes from transactions from all the tables that are part of the replication slot's publication are received in the order they were committed. This also implies ordered delivery across all the tablets that are part of the publication's table list. |
| At least once delivery | Changes from transactions are streamed at least once. Changes from transactions may be streamed again in case of restart after failure. For example, this can happen in the case of a Kafka Connect node failure. If the Kafka Connect node pushes the records to Kafka and crashes before committing the offset, it will again get the same set of records upon restart. |
| No gaps in change stream | Receiving changes that are part of a transaction with commit time *t* implies that you have already received changes from all transactions with commit time lower than *t*. Thus, receiving any change for a row with commit timestamp *t* implies that you have received all older changes for that row. |

## Key Concepts

Understanding key concepts like replication slots, publications, replica identity, and LSNs is crucial for managing CDC effectively.

{{<lead link="./key-concepts/">}}
Review [key concepts](./key-concepts/) of YugabyteDB CDC with logical replication.
{{</lead>}}

## Getting Started

Get up and running quickly with your first CDC deployment.

{{<lead link="./get-started/">}}
[Get started](./get-started/) with the YugabyteDB Connector.
{{</lead>}}

## Set Up and Configure CDC

Configure your cluster for CDC with gflags, retention policies, and use-case-specific tuning.

{{<lead link="./setup-configuration/">}}
[Setup and Configuration](./setup-configuration/) - Gflags reference, retention policies, and tuning by use case.
{{</lead>}}

## Operational Procedures

Learn how to safely manage your CDC deployment during active replication.

{{<lead link="./operational-procedures/">}}
[Operational Procedures](./operational-procedures/) - Safe DDL operations, publication management, slot recovery, and troubleshooting.
{{</lead>}}

## YugabyteDB Connector

Stream your changes to Kafka and other external systems using the YugabyteDB Connector.

{{<lead link="./yugabytedb-connector/">}}
[YugabyteDB Connector](./yugabytedb-connector/) - Kafka Connect integration and configuration reference.
{{</lead>}}

## Best Practices

Optimize performance, reliability, and resource usage.

{{<lead link="./best-practices/">}}
[Best Practices](./best-practices/) - Parallel consumption, fan-out patterns, load balancing, and Kafka strategies.
{{</lead>}}

## Advanced Topics

Dive deeper into schema evolution, snapshots, replication origins, and DDL streaming.

{{<lead link="./advanced-topic/">}}
[Advanced Topics](./advanced-topic/) - Architectural details and advanced configuration scenarios.
{{</lead>}}

## Monitoring

Track metrics, lag, and health of your CDC deployment.

{{<lead link="./monitor/">}}
[Monitoring](./monitor/) - Metrics, endpoints, and health checks.
{{</lead>}}

## Key Limitations by Version

| Feature | v2024.2 | v2025.1 | v2025.2 | v2026.1 |
|---------|---------|---------|---------|---------|
| Replica identity | PK only | Full support | Full support | Full support |
| Table schema evolution | Limited | Limited | Limited | Full (non-colocated) |
| DDL rewrite blocking | Blocked | Blocked | Blocked | Non-blocking |
| Intra-txn before-image | Manual | Manual | Auto | Auto |
| Savepoints | No | No | Yes (v2.2.0+) | Yes |
| Implicit publication changes | N/A | No | No | Yes |

See [Setup and Configuration - Limitations](./setup-configuration/#limitations-by-version) for complete details.

### Common Limitations

- **LSN not comparable across slots:** Use separate Kafka topics for each slot to avoid missed records.
- **Transactional DDL not supported:** Do not enable `ysql_yb_ddl_transaction_block_enabled`.
- **Unsupported functions:** `pg_current_wal_lsn`, `pg_wal_lsn_diff`, `IDENTIFY SYSTEM`, `txid_current`, `pg_stat_replication`.
- **Table rewrites:** v2026.1+ allows non-blocking rewrites for non-colocated tables; earlier versions require slot drop.
- **Single consumer per slot:** Multiple consumers will cause conflicts; use multiple slots for parallel consumption.
- **xCluster conflict:** CDC and xCluster cannot both replicate the same table.
- **YCQL not supported:** Only YSQL tables are supported.

### CDC with Point-in-Time Recovery

[Point-in-time recovery](../../../manage/backup-restore/point-in-time-recovery/) allows you to restore to a specific point in time. For databases with logical replication configured, create new replication slots after the restore is complete and start streaming from that point.

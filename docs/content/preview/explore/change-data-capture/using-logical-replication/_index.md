---
title: CDC using PostgreSQL replication protocol
headerTitle: CDC using PostgreSQL replication protocol
linkTitle: PostgreSQL protocol
description: CDC using YugabyteDB PostgreSQL replication protocol.
headcontent: Capture changes made to data in the database
cascade:
  earlyAccess: /preview/releases/versioning/#feature-maturity
menu:
  preview:
    identifier: explore-change-data-capture-logical-replication
    parent: explore-change-data-capture
    weight: 240
type: indexpage
showRightNav: true
---

## Overview

YugabyteDB CDC captures changes made to data in the database and streams those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB database to downstream consumers based on its Write-Ahead Log (WAL). YugabyteDB CDC captures row-level changes resulting from INSERT, UPDATE, and DELETE operations in the configured database and publishes it further to be consumed by downstream applications.

### Highlights

#### Resilience

1. Following a failure of the application or server or n/w, the replication can continue from any of the available server nodes.

2. Replication continues from the transaction immediately after the transaction that was last acknowledged by the application. There will be no transaction that will be missed by the application.

#### Security

CDC in YugabyteDB being based on the PostgreSQL Logical Replication model means:

1. CDC user persona will be a 'PG Replication client'.

2. A standard replication connection is used for consumption, and all the server-side configurations of authentication, authorizations, SSL modes, and connection load balancing can be leveraged automatically.

#### Guarantees

| GUARANTEE | DESCRIPTION |
| :----- | :----- |
| Per-slot ordered delivery guarantee | Changes from transactions from all the tables that are part of the replication slot’s publication are received in the order they were committed. This also implies ordered delivery across all the tablets that are part of the publication’s table list. |
| At least once delivery | Changes from transactions are streamed at least once. Changes from transactions may be streamed again in case of restart after failure. For example, this can happen in the case of a Kafka Connect node failure. If the Kafka Connect node pushes the records to Kafka and crashes before committing the offset, it will again get the same set of records upon restart. |
| No gaps in change stream | Receiving changes that are part of a transaction with commit time *t* implies that you have already received changes from all transactions with commit time lower than *t*. Thus, receiving any change for a row with commit timestamp *t*,  implies that you have received all older changes for that row. |

## Key concepts

The YugabyteDB logical replication feature makes use of PostgreSQL concepts like replication slot, publication, replica identity, and so on. Understanding these key concepts is crucial for setting up and managing a logical replication environment effectively.

{{<lead link="./key-concepts">}}
To know more about the key concepts of YugabyteDB CDC with logical replication, see [Key concepts](./key-concepts)
{{</lead>}}

## Getting started

Get started with YugabyteDB logical replication using the YugabyteDB Connector.

{{<lead link="./get-started">}}

To learn how get started with the connector, see [Get started](./get-started).

{{</lead>}}

## Monitoring

You can monitor the activities and status of the deployed connectors using the http end points provided by YugabyteDB.

{{<lead link="./monitor">}}
To know more about how to monitor your CDC setup, see [Monitor](./monitor/).
{{</lead>}}

## YugabyteDB Connector

To capture and stream your changes in YugabyteDB to an external system, you need a connector that can read the changes in YugabyteDB and stream it out. For this, you can use the YugabyteDB Connector, which is based on the Debezium platform. The connector is deployed as a set of Kafka Connect-compatible connectors, so you first need to define a YugabyteDB connector configuration and then start the connector by adding it to Kafka Connect.

{{<lead link="./yugabytedb-connector/">}}
To understand how the various features and configuration of the connector, see [YugabyteDB Connector](./yugabytedb-connector/).
{{</lead>}}

## Limitations

- LSN Comparisons Across Slots.

    In the case of YugabyteDB, the LSN  does not represent the byte offset of a WAL record. Hence, arithmetic on LSN and any other usages of the LSN making this assumption will not work. Also, currently, comparison of LSN values from messages coming from different replication slots is not supported.

- The following functions are currently unsupported:

  - `pg_current_wal_lsn`
  - `pg_wal_lsn_diff`
  - `IDENTIFY SYSTEM`
  - `txid_current`
  - `pg_stat_replication`

  Additionally, the functions responsible for pulling changes instead of the server streaming it are unsupported as well. They are described in [Replication Functions](https://www.postgresql.org/docs/11/functions-admin.html#FUNCTIONS-REPLICATION) in the PostgreSQL documentation.

- Restriction on DDLs

    DDL operations should not be performed from the time of replication slot creation till the start of snapshot consumption of the last table.

- There should be a primary key on the table you want to stream the changes from.

- CDC is not supported on a target table for xCluster replication [11829](https://github.com/yugabyte/yugabyte-db/issues/11829).

- Currently we don't support schema evolution for changes that require table rewrites (ex: ALTER TYPE).

- YCQL tables aren't currently supported. Issue [11320](https://github.com/yugabyte/yugabyte-db/issues/11320).

- Support for point-in-time recovery (PITR) is tracked in issue [10938](https://github.com/yugabyte/yugabyte-db/issues/10938).

- Support for transaction savepoints is tracked in issue [10936](https://github.com/yugabyte/yugabyte-db/issues/10936).

- Support for enabling CDC on Read Replicas is tracked in issue [11116](https://github.com/yugabyte/yugabyte-db/issues/11116).
---
title: CDC using PostgreSQL replication protocol
headerTitle: CDC using PostgreSQL replication protocol
linkTitle: PostgreSQL protocol
description: CDC using YugabyteDB PostgreSQL replication protocol.
headcontent: Capture changes made to data in the database
tags:
  feature: early-access
menu:
  v2025.1:
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

## Key concepts

The YugabyteDB logical replication feature makes use of PostgreSQL concepts like replication slot, publication, replica identity, and so on. Understanding these key concepts is crucial for setting up and managing a logical replication environment effectively.

{{<lead link="./key-concepts/">}}
Review [key concepts](./key-concepts) of YugabyteDB CDC with logical replication.
{{</lead>}}

## Getting started

Get started with YugabyteDB logical replication using the YugabyteDB Connector.

{{<lead link="./get-started/">}}
[Get started](./get-started) using the connector.
{{</lead>}}

## Monitoring

You can monitor the activities and status of the deployed connectors using the http end points provided by YugabyteDB.

{{<lead link="./monitor/">}}
Learn how to [monitor](./monitor/) your CDC setup.
{{</lead>}}

## YugabyteDB Connector

To capture and stream your changes in YugabyteDB to an external system, you need a connector that can read the changes in YugabyteDB and stream it out. For this, you can use the YugabyteDB Connector, which is based on the Debezium platform. The connector is deployed as a set of Kafka Connect-compatible connectors, so you first need to define a YugabyteDB connector configuration and then start the connector by adding it to Kafka Connect.

{{<lead link="./yugabytedb-connector/">}}
For reference documentation, see [YugabyteDB Connector](./yugabytedb-connector/).
{{</lead>}}

## Limitations

- Log Sequence Number ([LSN](../using-logical-replication/key-concepts/#lsn-type)) Comparisons Across Slots.

    In the case of YugabyteDB, the LSN  does not represent the byte offset of a WAL record. Hence, arithmetic on LSN and any other usages of the LSN making this assumption will not work. Also, currently, comparison of LSN values from messages coming from different replication slots is not supported.

- The following functions are currently unsupported:

  - `pg_current_wal_lsn`
  - `pg_wal_lsn_diff`
  - `IDENTIFY SYSTEM`
  - `txid_current`
  - `pg_stat_replication`

  Additionally, the functions responsible for pulling changes instead of the server streaming it are unsupported as well. They are described in [Replication Functions](https://www.postgresql.org/docs/15/functions-admin.html#FUNCTIONS-REPLICATION) in the PostgreSQL documentation.

- Restriction on DDLs

    DDL operations should not be performed from the time of replication slot creation till the start of snapshot consumption of the last table.

- There should be a primary key on the table you want to stream the changes from.

- CDC is not supported on tables that are also the target of xCluster replication (see issue {{<issue 15534>}}). However, both CDC and xCluster can work simultaneously on the same source tables.

    When performing [switchover](../../../deploy/multi-dc/async-replication/async-transactional-switchover/) or [failover](../../../deploy/multi-dc/async-replication/async-transactional-failover/) on xCluster, if you are using CDC, remember to also reconfigure CDC to use the new primary universe.

- Currently, CDC doesn't support schema evolution for changes that require table rewrites (for example, [ALTER TYPE](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-type-with-table-rewrite)), or DROP TABLE and TRUNCATE TABLE operations after the replication slot is created. However, you can perform these operations before creating the replication slot without any issues.

- YCQL tables aren't currently supported. Issue {{<issue 11320>}}.

- Support for point-in-time recovery (PITR) is tracked in issue {{<issue 10938>}}.

- Transaction savepoints are supported starting from v2025.1.3.0. Issue {{<issue 10936>}}.

- Support for enabling CDC on Read Replicas is tracked in issue {{<issue 11116>}}.

- Support for tablet splitting with logical replication is disabled from v2024.1.4 and v2024.2.1. Tracked in issue {{<issue 24918>}}.

- A replication slot should be consumed by at most one consumer at a time. However, there is currently no locking mechanism to enforce this. As a result, you should ensure that multiple consumers do not consume from a slot simultaneously. Tracked in issue {{<issue 20755>}}.

- If a row is updated or deleted in the same transaction in which it was inserted, CDC cannot retrieve the before-image values for the UPDATE / DELETE event. If the replica identity is not CHANGE, then CDC will throw an error while processing such events.

    To handle updates/deletes with a non-CHANGE replica identity, set the YB-TServer flag `cdc_send_null_before_image_if_not_exists` to true. With this flag enabled, CDC will send a null before-image instead of failing with an error.

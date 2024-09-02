---
title: Overview of CDC in YugabyteDB
headerTitle: Overview
linkTitle: Overview
description: Change Data Capture in YugabyteDB.
headcontent: Change Data Capture in YugabyteDB
menu:
  v2.20:
    parent: explore-change-data-capture
    identifier: cdc-overview
    weight: 10
type: docs
---

## What is change data capture?

In databases, change data capture (CDC) is a set of software design patterns used to determine and track the data that has changed so that action can be taken using the changed data. YugabyteDB CDC captures changes made to data in the database and streams those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB database to downstream consumers based on its Write-Ahead Log (WAL).

YugabyteDB CDC uses Debezium to capture row-level changes resulting from INSERT, UPDATE, and DELETE operations in the upstream database, and publishes them as events to Kafka using Kafka Connect-compatible connectors.

![What is CDC](/images/explore/cdc-overview-what.png)

Debezium is deployed as a set of Kafka Connect-compatible connectors, so you first need to define a YugabyteDB connector configuration and then start the connector by adding it to Kafka Connect.

## How does CDC work?

YugabyteDB automatically splits user tables into multiple shards (also called tablets) using either a hash- or range-based strategy. The primary key for each row in the table uniquely identifies the location of the tablet in the row.

Each tablet has its own WAL file. WAL is NOT in-memory, but it is disk persisted. Each WAL preserves the order in which transactions (or changes) happened. Hybrid TS, Operation ID, and additional metadata about the transaction is also preserved.

![How does CDC work](/images/explore/cdc-overview-work2.png)

YugabyteDB normally purges WAL segments after some period of time. This means that the connector does not have the complete history of all changes that have been made to the database. Therefore, when the connector first connects to a particular YugabyteDB database, it starts by performing a consistent snapshot of each of the database schemas.

The Debezium YugabyteDB connector captures row-level changes in the schemas of a YugabyteDB database. The first time it connects to a YugabyteDB cluster, the connector takes a consistent snapshot of all schemas. After that snapshot is complete, the connector continuously captures row-level changes that insert, update, and delete database content, and that were committed to a YugabyteDB database.

![How does CDC work](/images/explore/cdc-overview-work.png)

The connector produces a change event for every row-level insert, update, and delete operation that was captured, and sends change event records for each table in a separate Kafka topic. Client applications read the Kafka topics that correspond to the database tables of interest, and can react to every row-level event they receive from those topics. For each table, the default behavior is that the connector streams all generated events to a separate Kafka topic for that table. Applications and services consume data change event records from that topic.

The core primitive of CDC is the _stream_. Streams can be enabled and disabled on databases. Every change to a watched database table is emitted as a record in a configurable format to a configurable sink. Streams scale to any YugabyteDB cluster independent of its size and are designed to impact production traffic as little as possible.

![How does CDC work](/images/explore/cdc-overview-work3.png)

## Known limitations

* A single stream can only be used to stream data from one namespace only.
* There should be a primary key on the table you want to stream the changes from.
* CDC is not supported on a target table for xCluster replication [11829](https://github.com/yugabyte/yugabyte-db/issues/11829).
* Currently, CDC doesn't support schema evolution for changes that require table rewrites (for example, [ALTER TYPE](../../../v2.20/api/ysql/the-sql-language/statements/ddl_alter_table#alter-type-with-table-rewrite)), or DROP TABLE and TRUNCATE TABLE operations.
* YCQL tables aren't currently supported. Issue [11320](https://github.com/yugabyte/yugabyte-db/issues/11320).

In addition, CDC support for the following features will be added in upcoming releases:

* Support for point-in-time recovery (PITR) is tracked in issue [10938](https://github.com/yugabyte/yugabyte-db/issues/10938).
* Support for transaction savepoints is tracked in issue [10936](https://github.com/yugabyte/yugabyte-db/issues/10936).
* Support for enabling CDC on Read Replicas is tracked in issue [11116](https://github.com/yugabyte/yugabyte-db/issues/11116).
* Support for schema evolution with before image is tracked in issue [15197](https://github.com/yugabyte/yugabyte-db/issues/15197).

## Further reading

* Refer to [CDC Examples](https://github.com/yugabyte/cdc-examples/tree/main) for CDC usage and pattern examples.
* Refer to [Tutorials](/preview/tutorials/cdc-tutorials/) to deploy in different Kafka environments.
* Refer to blogs about CDC:
  * [Data Streaming Using YugabyteDB CDC, Kafka, and SnowflakeSinkConnector](https://www.yugabyte.com/blog/data-streaming-using-yugabytedb-cdc-kafka-and-snowflakesinkconnector/)
  * [Unlock Azure Storage Options With YugabyteDB CDC](https://www.yugabyte.com/blog/unlocking-azure-storage-options-with-yugabytedb-cdc/)
  * [Change Data Capture From YugabyteDB to Elasticsearch](https://www.yugabyte.com/blog/change-data-capture-cdc-yugabytedb-elasticsearch/)
  * [Snowflake CDC: Publishing Data Using Amazon S3 and YugabyteDB](https://www.yugabyte.com/blog/snowflake-cdc-publish-data-using-amazon-s3-yugabytedb/)
  * [Streaming Changes From YugabyteDB to Downstream Databases](https://www.yugabyte.com/blog/streaming-changes-yugabytedb-cdc-downstream-databases/)
  * [Change Data Capture from YugabyteDB CDC to ClickHouse](https://www.yugabyte.com/blog/change-data-capture-cdc-yugabytedb-clickhouse/)
  * [How to Run Debezium Server with Kafka as a Sink](https://www.yugabyte.com/blog/change-data-capture-cdc-run-debezium-server-kafka-sink/)
  * [Change Data Capture Using a Spring Data Processing Pipeline](https://www.yugabyte.com/blog/change-data-capture-cdc-spring-data-processing-pipeline/)

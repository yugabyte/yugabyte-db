---
title: CDC using YugabyteDB gRPC replication protocol
headerTitle: CDC using gRPC replication protocol
linkTitle: gRPC protocol
description: CDC using YugabyteDB gRPC replication protocol.
headcontent: Capture changes made to data in the database
tags:
  feature: early-access
aliases:
  - /stable/explore/change-data-capture/cdc-overview/
  - /stable/explore/change-data-capture/using-yugabytedb-grpc-replication/
menu:
  stable:
    identifier: explore-change-data-capture-grpc-replication
    parent: explore-change-data-capture
    weight: 280
type: indexpage
showRightNav: true
---

YugabyteDB CDC captures changes made to data in the database and streams those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB database to downstream consumers based on its Write-Ahead Log (WAL). YugabyteDB CDC uses Debezium to capture row-level changes resulting from INSERT, UPDATE, and DELETE operations in the upstream database, and publishes them as events to Kafka using Kafka Connect-compatible connectors.

![What is CDC](/images/explore/cdc-overview-work.png)

<!--
{{<lead link="./cdc-overview">}}
To know more about the internals of CDC, see [Overview](./cdc-overview).
{{</lead>}}
-->

## Get started

Get started with Yugabyte gRPC replication.

For tutorials on streaming data to Kafka environments, including Amazon MSK, Azure Event Hubs, and Confluent Cloud, see [Kafka environments](/stable/develop/tutorials/cdc-tutorials/).

{{<lead link="./cdc-get-started/">}}
[Get started](./cdc-get-started) using the connector.
{{</lead>}}

## Monitoring

You can monitor the activities and status of the deployed connectors using the http end points provided by YugabyteDB.

{{<lead link="./cdc-monitor/">}}
Learn how to [monitor](./cdc-monitor/) your CDC setup.
{{</lead>}}

## YugabyteDB gRPC Connector

To capture and stream your changes in YugabyteDB to an external system, you need a connector that can read the changes in YugabyteDB and stream it out. For this, you can use the YugabyteDB gRPC connector, which is based on the Debezium platform. The connector is deployed as a set of Kafka Connect-compatible connectors, so you first need to define a YugabyteDB connector configuration and then start the connector by adding it to Kafka Connect.

{{<lead link="./debezium-connector-yugabytedb/">}}
For reference documentation, see [YugabyteDB gRPC Connector](./debezium-connector-yugabytedb/).
{{</lead>}}

## Known limitations

* A single stream can only be used to stream data from one namespace only.
* There should be a primary key on the table you want to stream the changes from.
* CDC is not supported on tables that are also the target of xCluster replication (issue {{<issue 15534>}}). However, both CDC and xCluster can work simultaneously on the same source tables.

    When performing [switchover](../../../deploy/multi-dc/async-replication/async-transactional-switchover/) or [failover](../../../deploy/multi-dc/async-replication/async-transactional-failover/) on xCluster, if you are using CDC, remember to also reconfigure CDC to use the new primary universe.

* Currently, CDC doesn't support schema evolution for changes that require table rewrites (for example, [ALTER TYPE](../../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-type-with-table-rewrite)), or DROP TABLE and TRUNCATE TABLE operations.
* YCQL tables aren't currently supported. Issue {{<issue 11320>}}.
* [Composite types](../../../explore/ysql-language-features/data-types#composite-types) are currently not supported. Issue {{<issue 25221>}}.

* If a row is updated or deleted in the same transaction in which it was inserted, CDC cannot retrieve the before-image values for the UPDATE / DELETE event. If the replica identity is not CHANGE, then CDC will throw an error while processing such events.

    To handle updates/deletes with a non-CHANGE replica identity, set the YB-TServer flag `cdc_send_null_before_image_if_not_exists` to true. With this flag enabled, CDC will send a null before-image instead of failing with an error.

In addition, CDC support for the following features will be added in upcoming releases:

* Support for point-in-time recovery (PITR) is tracked in issue {{<issue 10938>}}.
* Transaction savepoints are supported starting from v2025.2.1.0. Issue {{<issue 10936>}}.
* Support for enabling CDC on Read Replicas is tracked in issue {{<issue 11116>}}.
* Support for schema evolution with before image is tracked in issue {{<issue 15197>}}.

## Learn more

* [CDC architecture](../../../architecture/docdb-replication/change-data-capture/)
* [Examples of CDC usage and patterns](https://github.com/yugabyte/cdc-examples/tree/main) {{<icon/github>}}
* [Tutorials to deploy in different Kafka environments](/stable/develop/tutorials/cdc-tutorials/) {{<icon/tutorial>}}
* [Data Streaming Using YugabyteDB CDC, Kafka, and SnowflakeSinkConnector](https://www.yugabyte.com/blog/data-streaming-using-yugabytedb-cdc-kafka-and-snowflakesinkconnector/) {{<icon/blog>}}
* [Unlock Azure Storage Options With YugabyteDB CDC](https://www.yugabyte.com/blog/unlocking-azure-storage-options-with-yugabytedb-cdc/) {{<icon/blog>}}
* [Change Data Capture From YugabyteDB to Elasticsearch](https://www.yugabyte.com/blog/change-data-capture-cdc-yugabytedb-elasticsearch/) {{<icon/blog>}}
* [Snowflake CDC: Publishing Data Using Amazon S3 and YugabyteDB](https://www.yugabyte.com/blog/snowflake-cdc-publish-data-using-amazon-s3-yugabytedb/) {{<icon/blog>}}
* [Streaming Changes From YugabyteDB to Downstream Databases](https://www.yugabyte.com/blog/streaming-changes-yugabytedb-cdc-downstream-databases/) {{<icon/blog>}}
* [Change Data Capture from YugabyteDB CDC to ClickHouse](https://www.yugabyte.com/blog/change-data-capture-cdc-yugabytedb-clickhouse/) {{<icon/blog>}}
* [How to Run Debezium Server with Kafka as a Sink](https://www.yugabyte.com/blog/change-data-capture-cdc-run-debezium-server-kafka-sink/) {{<icon/blog>}}
* [Change Data Capture Using a Spring Data Processing Pipeline](https://www.yugabyte.com/blog/change-data-capture-cdc-spring-data-processing-pipeline/) {{<icon/blog>}}

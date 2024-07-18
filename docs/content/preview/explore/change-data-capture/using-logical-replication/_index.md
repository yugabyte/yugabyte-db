---
title: Using logical replication
headerTitle: Using logical replication
linkTitle: Using logical replication
description: CDC or Change data capture is a process to capture changes made to data in the database.
headcontent: Capture changes made to data in the database
image: /images/section_icons/index/develop.png
cascade:
  earlyAccess: /preview/releases/versioning/#feature-maturity
menu:
  preview:
    identifier: explore-change-data-capture-logical-replication
    parent: explore-change-data-capture
    weight: 240
type: indexpage
---

{{< note title="Note for internal contribution" >}}

This page is currently under development and is most likely that the content is not up to date with what the heading intends.

{{< /note >}}

In databases, change data capture (CDC) is a set of software design patterns used to determine and track the data that has changed so that action can be taken using the changed data. CDC is beneficial in a number of scenarios. Let us look at few of them.

- **Microservice-oriented architectures** : Some microservices require a stream of changes to the data, and using CDC in YugabyteDB can provide consumable data changes to CDC subscribers.

- **Asynchronous replication to remote systems** : Remote systems may subscribe to a stream of data changes and then transform and consume the changes. Maintaining separate database instances for transactional and reporting purposes can be used to manage workload performance.

- **Multiple data center strategies** : Maintaining multiple data centers enables enterprises to provide high availability (HA).

- **Compliance and auditing** : Auditing and compliance requirements can require you to use CDC to maintain records of data changes.

{{<index/block>}}

  {{<index/item
    title="Get started"
    body="Get set up for using CDC in YugabyteDB."
    href="cdc-log-rep-get-started/"
    icon="/images/section_icons/index/quick_start.png">}}

  {{<index/item
    title="Section undecided"
    body="How to stream data with different Kafka environments."
    href="../../tutorials/cdc-tutorials/"
    icon="/images/section_icons/develop/ecosystem/apache-kafka-icon.png">}}

{{</index/block>}}

## How does CDC work

YugabyteDB CDC captures changes made to data in the database and streams those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB database to downstream consumers based on its Write-Ahead Log (WAL). YugabyteDB CDC uses Debezium to capture row-level changes resulting from INSERT, UPDATE, and DELETE operations in the upstream database, and publishes them as events to Kafka using Kafka Connect-compatible connectors.

![What is CDC](/images/explore/cdc-overview-what.png)

{{<lead link="./cdc-overview">}}
To know more about the internals of CDC, see [Overview](./cdc-overview).
{{</lead>}}

## Debezium connector

To capture and stream your changes in YugabyteDB to an external system, you need a connector that can read the changes in YugabyteDB and stream it out. For this, you can use the Debezium connector. Debezium is deployed as a set of Kafka Connect-compatible connectors, so you first need to define a YugabyteDB connector configuration and then start the connector by adding it to Kafka Connect.

{{<lead link="./debezium-connector-postgresql">}}
To understand how the various features and configuration of the connector, see [Debezium connector](./debezium-connector-postgresql).
{{</lead>}}

## Monitoring

You can monitor the activities and status of the deployed connectors using the http end points provided by YugabyteDB.

{{<lead link="./cdc-monitor">}}
To know more about how to monitor your CDC setup, see [Monitor](./cdc-monitor).
{{</lead>}}

For tutorials on streaming data to Kafka environments, including Amazon MSK, Azure Event Hubs, and Confluent Cloud, see [Kafka environments](/preview/tutorials/cdc-tutorials/).

## Learn more

- [Examples of CDC usage and patterns](https://github.com/yugabyte/cdc-examples/tree/main) {{<icon/github>}}
- [Tutorials to deploy in different Kafka environments](../../tutorials/cdc-tutorials/) {{<icon/tutorial>}}
- [Data Streaming Using YugabyteDB CDC, Kafka, and SnowflakeSinkConnector](https://www.yugabyte.com/blog/data-streaming-using-yugabytedb-cdc-kafka-and-snowflakesinkconnector/) {{<icon/blog>}}
- [Unlock Azure Storage Options With YugabyteDB CDC](https://www.yugabyte.com/blog/unlocking-azure-storage-options-with-yugabytedb-cdc/) {{<icon/blog>}}
- [Change Data Capture From YugabyteDB to Elasticsearch](https://www.yugabyte.com/blog/change-data-capture-cdc-yugabytedb-elasticsearch/) {{<icon/blog>}}
- [Snowflake CDC: Publishing Data Using Amazon S3 and YugabyteDB](https://www.yugabyte.com/blog/snowflake-cdc-publish-data-using-amazon-s3-yugabytedb/) {{<icon/blog>}}
- [Streaming Changes From YugabyteDB to Downstream Databases](https://www.yugabyte.com/blog/streaming-changes-yugabytedb-cdc-downstream-databases/) {{<icon/blog>}}
- [Change Data Capture from YugabyteDB CDC to ClickHouse](https://www.yugabyte.com/blog/change-data-capture-cdc-yugabytedb-clickhouse/) {{<icon/blog>}}
- [How to Run Debezium Server with Kafka as a Sink](https://www.yugabyte.com/blog/change-data-capture-cdc-run-debezium-server-kafka-sink/) {{<icon/blog>}}
- [Change Data Capture Using a Spring Data Processing Pipeline](https://www.yugabyte.com/blog/change-data-capture-cdc-spring-data-processing-pipeline/) {{<icon/blog>}}

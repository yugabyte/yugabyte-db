---
title: Use change data capture with YugabyteDB Aeon
headerTitle: Change data capture
linkTitle: Change data capture
description: Stream data change events from a YugabyteDB Aeon cluster.
headcontent: Stream data change events from a YugabyteDB Aeon cluster
menu:
  preview_yugabyte-cloud:
    identifier: aeon-cdc
    parent: cloud-clusters
    weight: 400
type: docs
---

YugabyteDB Aeon CDC captures changes made to data in the database and streams those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB Aeon database to downstream consumers based on its Write-Ahead Log (WAL). CDC captures row-level changes resulting from INSERT, UPDATE, and DELETE operations, and publishes them to be consumed by downstream applications.

## Prerequisites

- Cluster running YugabyteDB v2024.1.1 or later
- [YugabyteDB Connector v2.5.2](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.1)
- Kafka Connect. For instructions on setting up Kafka Connect, refer to [Start Kafka Connect](../../../explore/change-data-capture/using-logical-replication/get-started/#start-kafka-connect)

## Limitations

- You can have a maximum of two streams.

## Configure CDC

YugabyteDB Aeon clusters are already configured to support CDC. To create streams and begin propagating changes, you must set up the following:

- YugabyteDB Connector

### Set up the connector

To stream data change events from YugabyteDB databases, follow these steps to deploy the YugabyteDB gRPC Connector:

1. Download the Connector from the [GitHub releases](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.1).
1. Extract and install the connector archive in your Kafka Connect environment.
1. Configure the Connector as follows:

    ```sh
    publication.autocreate.mode=<disabled/filtered/all_tables>
    connector.class=io.debezium.connector.postgresql.YugabyteDBConnector
    database.dbname=<database-name> 
    database.user=<database-admin-username>
    slot.name=<slot-name>
    tasks.max=1
    database.port=5433
    plugin.name=yboutput
    topic.prefix=<customer-specific>
    database.sslmode=require
    database.hostname=<cluster-host-name>
    database.password=<user-password>
    table.include.list=<schema.table1_name,schema.table2_name>
    snapshot.mode=initial
    yb.consistent.snapshot=false
    ```

    - `slot.name` - if a slot with the same name does not already exist, YugabyteDB Aeon creates it.
    - `publication.name` - provide a publication name if you have a publication already created.
    - `database.hostname` - the cluster hostname is displayed on the cluster **Settings** tab under **Connection Parameters**.
    - `snapshot.mode` - can be one of `Initial`, `Initial_only`, or `Never`. `Initial` requires the `yb.consistent.snapshot=false` setting.

1. Start the connector by adding the connector's configuration to Kafka Connect and start the connector.

For more details on deploying and configuring the connector, refer to the [YugabyteDB Connector documentation](../../../explore/change-data-capture/using-logical-replication/yugabytedb-connector/).



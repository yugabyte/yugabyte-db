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

You can use change data capture with YugabyteDB Aeon clusters to capture changes made to data in the database and stream those changes to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB Aeon database to downstream consumers based on its Write-Ahead Log (WAL). CDC captures row-level changes resulting from INSERT, UPDATE, and DELETE operations, and publishes them to be consumed by downstream applications.

## Overview

- key concepts explore/change-data-capture/using-logical-replication/key-concepts/
- monitor views explore/change-data-capture/using-logical-replication/monitor/
    CDC Service metrics - export from YBM/ Metrics tab

## Prerequisites

- Cluster running YugabyteDB v2024.1.1 or later

- Customer Kafka environment

  - Confluent Cloud or AWS MSK Connect or Self Hosted Kafka (explore/change-data-capture/using-logical-replication/get-started/#get-started-with-yugabytedb-connector)

- [YugabyteDB Connector v2.5.2](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.1)

## Limitations

By default, you have a maximum of two active replication slots. If you need more slots, contact YugabyteDB Support.

## Configure YugabyteDB Connector

YugabyteDB Aeon clusters are already configured to support CDC. To create streams and begin propagating changes, configure the YugabyteDB connector using a JSON file containing the connector configuration properties, including the connection parameters for your YugabyteDB Aeon cluster.

### Set up the connector

To stream data change events from YugabyteDB databases, register the YugabyteDB connector to start monitoring changes as follows:

1. Download the Connector JAR file from [GitHub releases](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.1).
1. Depending on your environment CC / AWS MSK
1. In your JSON file, configure the Connector as follows:

    ```json
    {
        "name": "ybconnector",
        "config": {
            "tasks.max": "1",
            "publication.autocreate.mode": "filtered",
            "connector.class": "io.debezium.connector.postgresql.YugabyteDBConnector",
            "database.dbname": "yugabyte",
            "database.hostname": "us-west1.35263f18-0233-418a-8256-a40d075ffcde.gcp.ybdb.io",
            "database.port": "5433",
            "database.user": "admin",
            "database.password": "P@ssw0rd",
            "database.sslmode": "require",
            "topic.prefix": "yb",
            "snapshot.mode": "initial",
            "yb.consistent.snapshot": false,
            "table.include.list": "public.orders,public.users",
            "plugin.name": "yboutput",
            "slot.name": "yb_replication_slot",
            "value.converter": "org.apache.kafka.connect.json.JsonConverter",
            "key.converter": "org.apache.kafka.connect.json.JsonConverter",
            "value.converter.schemas.enable": true,
            "key.converter.schemas.enable": true
        }
    }
    ```

    - `database.name` - the name of the YSQL database you want to monitor.
    - `database.user` - the username of a cluster admin.
    - `slot.name` - the name for the replication slot. If a slot with the same name does not already exist, YugabyteDB Aeon creates it (to a maximum of two).
    - `publication.name` - provide a publication name if you have a publication already created.
    - `database.hostname` - the cluster hostname is displayed on the cluster **Settings** tab under **Connection Parameters**.
    - `snapshot.mode` - can be one of `Initial`, `Initial_only`, or `Never`. `Initial` requires the `yb.consistent.snapshot=false` setting.
    - etc
    - table.include.list the names of the table in format schema.table

    For a full list of properties, refer to [Connector properties](../../../explore/change-data-capture/using-logical-replication/yugabytedb-connector-properties).

After the connector starts, it performs a consistent snapshot of the YugabyteDB databases that the connector is configured for. The connector then starts generating data change events for row-level operations and streaming change event records to Kafka topics.

For more details on deploying and configuring the connector, refer to the [YugabyteDB Connector documentation](../../../explore/change-data-capture/using-logical-replication/yugabytedb-connector/).

## Monitor

- Service side - YBM - new charts
- Connector side - CC or AWS MSK

## Manage CDC

- connector upgrades

## Remove CDC

## FAQ


## Establish a replication connection to the database

To be able to send [replication commands](https://www.postgresql.org/docs/11/protocol-replication.html) to the database, you need to make a replication connection by adding the `replication=database` connection parameter to the connection string.

To do this, [connect to your cluster](../../cloud-connect/connect-client-shell/) using a client shell as you normally would, and add `replication=database` to the connection string. For example:

```sh
./ysqlsh "host=740ce33e-4242-4242-a424-cc4242c4242b.aws.ybdb.io \
    user=admin \
    dbname=yugabyte \
    sslmode=verify-full \
    sslrootcert=root.crt \
    replication=database"
```

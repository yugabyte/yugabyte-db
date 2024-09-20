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

  - [Self-managed Kafka](explore/change-data-capture/using-logical-replication/get-started/#get-started-with-yugabytedb-connector), or a managed service such as Confluent Cloud or AWS MSK Connect.

- [YugabyteDB Connector v2.5.2](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.1)

  - Download the Connector JAR file from [GitHub releases](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.1).

## Limitations

By default, you have a maximum of two active replication slots. If you need more slots, contact YugabyteDB Support.

## Configure change data capture

YugabyteDB Aeon clusters are already configured to support CDC. To create streams and begin propagating changes, first configure the YugabyteDB connector settings using a JSON file containing the connector configuration properties, including the connection parameters for your YugabyteDB Aeon cluster.

### Create Kafka topics

If auto creation of topics is not enabled in the Kafka Connect cluster then you need to create the following topics in Kafka manually:

- Topic for each table in the format `<topic.prefix>.<schemaName>.<tableName>`.
- Heartbeat topic in the format `<topic.heartbeat.prefix>.<topic.prefix>`. The `topic.heartbeat.prefix` has a default value of `__debezium-heartbeat`.

### Configure the connector

The connector is configured using a configuration file in JSON format. The file defines the settings for the connector to use using a set of connector properties.

The following example shows the required and common properties:

```json
{
    "name": "ybconnector",
    "config": 
    {
        "tasks.max": "1",
        "publication.autocreate.mode": "filtered",
        "connector.class": "io.debezium.connector.postgresql.YugabyteDBConnector",
        "database.dbname": "yugabyte",
        "database.hostname": "<cluster-hostname>",
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

- `connector.class` - set to `io.debezium.connector.postgresql.YugabyteDBConnector`.
- `database.dbname` - the name of the YSQL database you want to monitor.
- `database.hostname` - the cluster hostname is displayed on the cluster **Settings** tab under **Connection Parameters**.
- `database.port` - the port to use; by default YugabyteDB uses port 5433 for YSQL.
- `database.user` - the username of a cluster admin.
- `database.password` - the user password.
- `database.sslmode` - the SSL mode to use; set to `require`.
- `slot.name` - the name for the replication slot. If a slot with the same name does not already exist, YugabyteDB Aeon creates it (to a maximum of two).
- `publication.name` - provide a publication name if you have a publication already created.
- `snapshot.mode` - can be one of `Initial`, `Initial_only`, or `Never`. `Initial` requires the `yb.consistent.snapshot=false` setting.
- `table.include.list` - the names of the tables to monitor, comma-separated, in format `schema.table-name`.

For a full list of YugabyteDB Connector properties, refer to [Connector properties](../../../explore/change-data-capture/using-logical-replication/yugabytedb-connector-properties).

For more details on deploying and configuring the connector, refer to the [YugabyteDB Connector documentation](../../../explore/change-data-capture/using-logical-replication/yugabytedb-connector/).

### Configure provider

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#confluent" class="nav-link active" id="confluent-tab" data-bs-toggle="tab"
      role="tab" aria-controls="confluent" aria-selected="true">
      <i class="icon-confluent" aria-hidden="true"></i>
      Confluent Cloud
    </a>
  </li>
  <li >
    <a href="#msk" class="nav-link" id="msk-tab" data-bs-toggle="tab"
      role="tab" aria-controls="msk" aria-selected="false">
      <i class="icon-aws" aria-hidden="true"></i>
      Amazon MSK
    </a>
  </li>
  <li >
    <a href="#self" class="nav-link" id="self-tab" data-bs-toggle="tab"
      role="tab" aria-controls="self" aria-selected="false">
      <i class="icon-kafka" aria-hidden="true"></i>
      Self managed
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="confluent" class="tab-pane fade show active" role="tabpanel" aria-labelledby="confluent-tab">

To stream data change events from YugabyteDB databases, register the YugabyteDB connector with Confluent Cloud as follows:

To create a plugin:

1. In Confluent Cloud, navigate to your Kafka cluster, select **Connectors**, and click **Add Plugin**.
1. Enter a name and description for the plugin.
1. Set **Connector class** to `io.debezium.connector.postgresql.YugabyteDBConnector`.
1. Set **Connector type** to **Source**.
1. Click **Select connector archive** and upload the YugabyteDB Connector JAR file.

To set up the connector using the plugin:

1. In Confluent Cloud, navigate to your Kafka cluster, select **Connectors**, and click **Plugins**.
1. Select the plugin you created.
1. Specify an API key for the plugin to use and then click **Continue**.
1. Select the **JSON** tab and add the YugabyteDB Connector parameters between the curly braces. Click **Continue** when you are done.
1. In the **Endpoint** field, enter the hostname of your cluster. The cluster hostname is displayed on the cluster **Settings** tab under **Connection Parameters**.
1. Finish the setup.

After the connector starts, it performs a consistent snapshot of the YugabyteDB databases that the connector is configured for. The connector then starts generating data change events for row-level operations and streaming change event records to Kafka topics.

  </div>
  <div id="msk" class="tab-pane fade" role="tabpanel" aria-labelledby="msk-tab">

AWS MSK instructions

  </div>
  <div id="self" class="tab-pane fade" role="tabpanel" aria-labelledby="self-tab">

AWS MSK instructions

  </div>
</div>

## Monitor

- Service side - YBM - new charts and views explore/change-data-capture/using-logical-replication/monitor/

Add queries for views - esp pg_publication_tables + pg_replication_slots

```sh
yugabyte=> select * from pg_replication_slots;
```

```output
      slot_name      |  plugin  | slot_type | datoid | database | temporary | active | active_pid | xmin | catalog_xmin | restart_lsn | confirmed_flush_lsn |           yb_stream_id           | yb_restart_commit_ht 
---------------------+----------+-----------+--------+----------+-----------+--------+------------+------+--------------+-------------+---------------------+----------------------------------+----------------------
 yb_replication_slot | yboutput | logical   |  13251 | yugabyte | f         | f      |            |    7 |            7 | 0/EA79      | 0/EA7A              | 0dc62aec28106aa8ba494b620769ec69 |  7072030957220536320
(1 row)
```

If you are using a managed Kafka service, you can also monitor from the connector side.

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

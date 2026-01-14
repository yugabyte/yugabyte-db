---
title: Use change data capture with YugabyteDB Aeon
headerTitle: Change data capture
linkTitle: Change data capture
description: Stream data change events from a YugabyteDB Aeon cluster.
headcontent: Stream data change events from a YugabyteDB Aeon cluster
tags:
  feature: early-access
menu:
  stable_yugabyte-cloud:
    identifier: aeon-cdc
    parent: cloud-clusters
    weight: 400
type: docs
---

Use change data capture with YugabyteDB Aeon clusters to capture and stream changes made to data in the database to external processes, applications, or other databases. CDC allows you to track and propagate changes in a YugabyteDB Aeon database to downstream consumers based on its Write-Ahead Log (WAL). CDC captures row-level changes resulting from INSERT, UPDATE, and DELETE operations, and publishes them to be consumed by downstream applications.

## Overview

YugabyteDB Aeon change data capture uses the [PostgreSQL Logical Replication](https://www.postgresql.org/docs/15/logical-replication.html) protocol. Logical replication uses a publish and subscribe model with one or more subscribers subscribing to one or more publications on a publisher node. Subscribers pull data from the publications they subscribe to and may subsequently re-publish data to allow cascading replication or more complex configurations.

It works as follows:

1. Create Publications in the YugabyteDB cluster as you would in PostgreSQL.
1. Deploy the [YugabyteDB Connector](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/) in your preferred Kafka Connect environment.
1. The connector uses replication slots to capture change events and publishes them directly to a Kafka topic.

A _publication_ is a set of changes generated from a table or a group of tables, and might also be described as a change set or replication set. Each publication exists in only one database. Publications are different from schemas and do not affect how the table is accessed. Each table can be added to multiple publications if needed. Publications only contain tables. Tables are added explicitly, except when a publication is created for ALL TABLES. Every publication can have multiple subscribers.

A _subscription_ is the downstream side of logical replication. The node where a subscription is defined is referred to as the subscriber. A subscription defines the connection to another database and set of publications (one or more) to which it wants to subscribe. Each subscription receives changes via one replication slot.

A _replication slot_ represents a stream of changes that can be replayed to a client in the order they were made on the origin server. Each slot streams a sequence of changes from a single database. You can initially create two replication slots per YugabyteDB Aeon cluster.

Logical replication of a table starts with taking a snapshot of the data on the publisher database and copying that to the subscriber. After that is done, the changes on the publisher are sent to the subscriber as they occur in real-time. The subscriber applies the data in the same order as the publisher so that transactional consistency is guaranteed for publications in a single subscription. This method of data replication is sometimes referred to as transactional replication.

For more information, refer to [How the connector works](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/#how-the-connector-works).

## Prerequisites

- Cluster running YugabyteDB v2024.1.1 or later.
  - If you have a new cluster running v2024.1.1 or later, CDC is available automatically.
  - If you have a cluster that was upgraded to v2024.1.1 or later and want to use CDC, contact {{% support-cloud %}}.

- Kafka environment. This can be a [Self-managed Kafka](../../../additional-features/change-data-capture/using-logical-replication/get-started/#get-started-with-yugabytedb-connector), or a managed service such as [Confluent Cloud](https://www.confluent.io/confluent-cloud/) or [AWS MSK Connect](https://aws.amazon.com/msk/features/msk-connect/).

- YugabyteDB Connector v2.5.2. Download the Connector JAR file from [GitHub releases](https://github.com/yugabyte/debezium/releases/tag/dz.2.5.2.yb.2024.1).

## Limitations

By default, you have a maximum of two active replication slots. If you need more slots, contact {{% support-cloud %}}.

CDC is not available for Sandbox clusters.

## Configure change data capture

YugabyteDB Aeon clusters are already configured to support CDC. To create streams and begin propagating changes, first configure the YugabyteDB connector settings using a JSON file containing the connector configuration properties. This includes the connection parameters for your YugabyteDB Aeon cluster.

### Create Kafka topics

If auto creation of topics is not enabled in the Kafka Connect cluster, then you need to create the following topics in Kafka manually:

- Topic for each table in the format `<topic.prefix>.<schemaName>.<tableName>`. See [topic.prefix](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector-properties/#topic-prefix).
- Heartbeat topic in the format `<topic.heartbeat.prefix>.<topic.prefix>`. The `topic.heartbeat.prefix` has a default value of `__debezium-heartbeat`. See [topic.heartbeat.prefix](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector-properties/#topic-heartbeat-prefix).

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
        "database.dbname": "<database-name>",
        "database.hostname": "<cluster-hostname>",
        "database.port": "5433",
        "database.user": "<username>",
        "database.password": "<password>",
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

| Parameter | Description |
| :--- | :--- |
| tasks.max | Set to 1. The YugabyteDB connector always uses a single task. |
| publication.autocreate.mode | Set to filtered. In this mode, if a publication exists, the connector uses it. If no publication exists, the connector creates a new publication for tables that match the current filter configuration. |
| connector.class | Set to `io.debezium.connector.postgresql.YugabyteDBConnector`. |
| database.dbname | The name of the YSQL database you want to monitor. |
| database.hostname | The cluster hostname is displayed on the cluster **Settings** tab under **Connection Parameters**. |
| database.port | The port to use; by default YugabyteDB uses 5433 for YSQL. |
| database.user | The username of a cluster admin. |
| database.password | The user password. |
| database.sslmode | The SSL mode to use; set to `require`. |
| topic.prefix | Set to `yb`. Used as the topic name prefix for all Kafka topics that receive records from this connector. |
| snapshot.mode | Specifies the criteria for performing a snapshot when the connector starts. Can be one of `Initial`, `Initial_only`, or `Never`. `Initial` requires `yb.consistent.snapshot` to be set to false. To learn more about the options for taking a snapshot when the connector starts, refer to [Snapshots](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/#snapshots). |
| yb.consistent.snapshot | If you are using CDC with YugabyteDB Aeon clusters in Initial snapshot mode, this property is required and must be set to false. |
| table.include.list | The names of the tables to monitor, comma-separated, in format `schema.table-name`. |
| plugin.name | Set to `yboutput`. The name of the YugabyteDB logical decoding plugin. |
| slot.name | The name for the replication slot. If a slot with the same name does not already exist, YugabyteDB Aeon creates it (to a maximum of two). |
| value.converter | Controls the format of the values in messages written to or read from Kafka. Set to `org.apache.kafka.connect.json.JsonConverter` to use JSON. |
| key.converter | Controls the format of the keys in messages written to or read from Kafka. Set to `org.apache.kafka.connect.json.JsonConverter` to use JSON. |
| value.converter.schemas.enable | Set to true to use schemas with JSON data format. |
| key.converter.schemas.enable | Set to true to use schemas with JSON data format. |
| publication.name | Provide a publication name if you have a publication already created. |

For a full list of YugabyteDB Connector properties, refer to [Connector properties](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector-properties).

### Configure the Kafka provider

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#confluent" class="nav-link active" id="confluent-tab" data-bs-toggle="tab"
      role="tab" aria-controls="confluent" aria-selected="true">
      <img src="/images/section_icons/develop/ecosystem/confluent-cloud.jpg" alt="Confluent Cloud Icon">
      Confluent Cloud
    </a>
  </li>
<!--  <li >
    <a href="#msk" class="nav-link" id="msk-tab" data-bs-toggle="tab"
      role="tab" aria-controls="msk" aria-selected="false">
      <i class="icon-aws" aria-hidden="true"></i>
      Amazon MSK
    </a>
  </li> -->
  <li >
    <a href="#self" class="nav-link" id="self-tab" data-bs-toggle="tab"
      role="tab" aria-controls="self" aria-selected="false">
      <img src="/images/section_icons/develop/ecosystem/apache-kafka-icon.png" alt="Kafka Icon">
      Self managed
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="confluent" class="tab-pane fade show active" role="tabpanel" aria-labelledby="confluent-tab">

To stream data change events from YugabyteDB databases, first create a plugin in Confluent Cloud, then register the YugabyteDB Connector with the plugin you created.

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
<!--
  <div id="msk" class="tab-pane fade" role="tabpanel" aria-labelledby="msk-tab">

<!--AWS MSK instructions

  </div>
-->
  <div id="self" class="tab-pane fade" role="tabpanel" aria-labelledby="self-tab">

For information on using the YugabyteDB Connector with a self-managed Kafka cluster, refer to:

- [Get started with YugabyteDB Connector](../../../additional-features/change-data-capture/using-logical-replication/get-started/#get-started-with-yugabytedb-connector)
- [YugabyteDB Connector reference documentation](../../../additional-features/change-data-capture/using-logical-replication/yugabytedb-connector/)

  </div>

</div>

## Monitor

YugabyteDB change data capture provides a set of views and metrics you can use to monitor replication.

For more information, refer to [Monitor](../../../additional-features/change-data-capture/using-logical-replication/monitor/).

If you are using a managed Kafka service, you can also monitor from the connector side. Consult your Kafka service documentation.

### Views

Use the following views to monitor replication.

| View                  | Description |
| :-------------------- | :--- |
| pg_publication        | Contains all publication objects contained in the database. |
| pg_publication_rel    | Contains mapping between publications and tables. This is a many-to-many mapping. |
| pg_publication_tables | Provides a list of all replication tables. |
| pg_replication_slots  | Provides a list of all replication slots that currently exist on the database cluster, along with their metadata. |

For example, to list the publication slots, use the following query:

```sql
yugabyte=> SELECT * FROM pg_replication_slots;
```

```output
      slot_name      |  plugin  | slot_type | datoid | database | temporary | active | active_pid | xmin | catalog_xmin | restart_lsn | confirmed_flush_lsn |           yb_stream_id           | yb_restart_commit_ht
---------------------+----------+-----------+--------+----------+-----------+--------+------------+------+--------------+-------------+---------------------+----------------------------------+----------------------
 yb_replication_slot | yboutput | logical   |  13251 | yugabyte | f         | f      |            |    7 |            7 | 0/EA79      | 0/EA7A              | 0dc62aec28106aa8ba494b620769ec69 |  7072030957220536320
(1 row)
```

### Charts

On the cluster **Metrics** tab, you can view the following metrics:

| Metric                    | Description |
| :------------------------ | :--- |
| cdcsdk_change_event_count | The number of records sent by the CDC Service. |
| cdcsdk_traffic_sent       | Total traffic sent, in bytes. |
| cdcsdk_event_lag_micros   | Lag, calculated by subtracting the timestamp of the latest record in the WAL of a tablet from the last record sent to the connector. |
| cdcsdk_expiry_time_ms     | The time left to read records from WAL is tracked by the Stream Expiry Time (ms). |
| cdcsdk_flush_lag          | This lag metric shows the difference between the timestamp of the latest record in the WAL and the replication slot's restart time, in seconds. |

## Manage CDC

### Establish a replication connection to the database

To be able to send [replication commands](https://www.postgresql.org/docs/15/protocol-replication.html) to the database, you need to make a replication connection by adding the `replication=database` connection parameter to the connection string.

To do this, [connect to your cluster](../../cloud-connect/connect-client-shell/) using a client shell as you normally would, and add `replication=database` to the connection string. For example:

```sh
./ysqlsh "host=740ce33e-4242-4242-a424-cc4242c4242b.aws.yugabyte.cloud \
    user=admin \
    dbname=yugabyte \
    sslmode=verify-full \
    sslrootcert=root.crt \
    replication=database"
```

### Permissions

By default, only the admin user can configure CDC streaming. For clusters created using YugabyteDB v2024.1.0 or later, you can additionally enable or disable replication for other users using the following functions:

- `enable_replication_role('username')` – Enables replication for the specified user.
- `disable_replication_role('username')` – Disables replication for the specified user.

For example, to enable replication for the user `replication_user`:

```sql
SELECT enable_replication_role('replication_user');
```

To disable replication for the user:

```sql
SELECT disable_replication_role('replication_user');
```

Note the following:

- Only the admin user (created when you created the cluster) can execute these functions.
- The functions are only available in the `yugabyte` database.
- The functions aren't available for clusters which are already on v2024.1.0 or later (created prior to April 2, 2025), only newly created clusters, or clusters that are newly upgraded to v2024.1.0.
- Because YugabyteDB Aeon [restricts access to superuser](../../cloud-secure-clusters/cloud-users/), you cannot set the REPLICATION attribute using CREATE or ALTER; you must use these functions.

## FAQ

### How do I turn off CDC?

To stop streaming changes, stop your connector on the client side. You don't need to make any changes to your YugabyteDB Aeon cluster, or drop your replication slots.

### How do I upgrade the YugabyteDB Connector?

To upgrade the YugabyteDB Connector for an existing CDC configuration:

1. Download the latest Connector JAR file from [GitHub releases](https://github.com/yugabyte/debezium/releases/).
1. Replace the older version with the new JAR file in your Kafka Connect environment.

    For example, if you are using Confluent Cloud, upload the new JAR file to the plugin you created.

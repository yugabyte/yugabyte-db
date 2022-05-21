# Change Data Capture in YugabyteDB

**Change data capture** (or **CDC** for short) enables capturing changes performed to the data stored in YugabyteDB. This document provides an overview of the approach YugabyteDB uses for providing change capture stream on tables that can be consumed by third party applications. This feature is useful in a number of scenarios such as:

### Microservice-oriented architectures

There are some microservices that require a stream of changes to the data. For example, a search system powered by a service such as [Elasticsearch](https://www.elastic.co/elasticsearch/) may be used in conjunction with the database which stores the transactions. The search system requires a stream of changes made to the data in YugabyteDB.

### Asynchronous replication to remote systems

Remote systems such as caches and analytics pipelines may subscribe to the stream of changes, transform them and consume these changes.

### Two datacenter deployments

Two datacenter deployments in YugabyteDB leverage change data capture at the core.

> Note that in this design, the terms "datacenter", "cluster" and "universe" will be used interchangeably. We assume here that each YB universe is deployed in a single datacenter.

## Setting up CDC

To set up CDC we use the CDC Streams, the commands for which can be found under [yb-admin](https://docs.yugabyte.com/preview/admin/yb-admin/#change-data-capture-cdc-commands).

### Debezium

[Debezium](https://debezium.io/) is the connector we are using to pull data out of YugabyteDB. Debezium is an open-source distributed platform at which we can point our database by providing some configuration. Debezium will start collecting the change events from the database and publish them to Kafka.

We need to provide the configuration values in the JSON format, the important ones are explained here:

**Example:**

```json
{
  "name": "ybconnector",
  "config": {
    "tasks.max":"3",
    "connector.class": "io.debezium.connector.yugabytedb.YugabyteDBConnector",

    // stream ID created using yb-admin
    "database.streamid":"c15e72299560429dba8b50152cf972d7",
    "database.hostname":"127.0.0.1", // to connect to the JDBC driver
    "database.port":"5433", // PG port for JDBC driver

    // comma separated values of master nodes i.e. host:port
    "database.master.addresses":"127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100",

    "database.user": "yugabyte", // username
    "database.password":"yugabyte", // password
    "database.dbname":"yugabyte", // database in which the table resides

    // logical name of the cluster, helps in creating topic name
    "database.server.name": "dbserver1",

    // tables to be included for streaming i.e. <schemaName>.<tableName>
    "table.include.list":"public.test_table"
  }
}
```

For a list of complete parameters that can be configured see [Debezium Connector for YugabyteDB](https://docs.yugabyte.com/preview/integrations/cdc/debezium/).

## Design

### Process Architecture

```
                          ╔═══════════════════════════════════════════╗
                          ║  Node #1                                  ║
                          ║  ╔════════════════╗ ╔══════════════════╗  ║
                          ║  ║    YB-Master   ║ ║    YB-TServer    ║  ║  CDC Service is stateless
    CDC Streams metadata  ║  ║  (Stores CDC   ║ ║  ╔═════════════╗ ║  ║           |
    replicated with Raft  ║  ║   metadata)    ║ ║  ║ CDC Service ║ ║  ║<----------'
             .----------->║  ║                ║ ║  ╚═════════════╝ ║  ║
             |            ║  ╚════════════════╝ ╚══════════════════╝  ║
             |            ╚═══════════════════════════════════════════╝
             |
             |
             |_______________________________________________.
             |                                               |
             V                                               V
  ╔═══════════════════════════════════════════╗    ╔═══════════════════════════════════════════╗
  ║  Node #2                                  ║    ║  Node #3                                  ║
  ║  ╔════════════════╗ ╔══════════════════╗  ║    ║  ╔════════════════╗ ╔══════════════════╗  ║
  ║  ║    YB-Master   ║ ║    YB-TServer    ║  ║    ║  ║    YB-Master   ║ ║    YB-TServer    ║  ║
  ║  ║  (Stores CDC   ║ ║  ╔═════════════╗ ║  ║    ║  ║  (Stores CDC   ║ ║  ╔═════════════╗ ║  ║
  ║  ║   metadata)    ║ ║  ║ CDC Service ║ ║  ║    ║  ║   metadata)    ║ ║  ║ CDC Service ║ ║  ║
  ║  ║                ║ ║  ╚═════════════╝ ║  ║    ║  ║                ║ ║  ╚═════════════╝ ║  ║
  ║  ╚════════════════╝ ╚══════════════════╝  ║    ║  ╚════════════════╝ ╚══════════════════╝  ║
  ╚═══════════════════════════════════════════╝    ╚═══════════════════════════════════════════╝

```

Every YB-TServer has a `CDC service` that is stateless. The main beta APIs provided by `CDC Service` are:

* `createCDCSDKStream` API for creating the stream on the database.
* `getChangesCDCSDK` API that will be use by the client to get the latest set of changes.

### Pushing changes to external systems

We are using Debezium which uses the implementation of our APIs such as `getChangesCDCSDK` to get the change events from YugabyteDB. Debezium then publishes the data to Kafka topics when can then be consumed by external applications.

Now since Debezium is built on top of Kafka, we also leverage its proven resilience, scalability and handling of huge amount of data.

### CDC Guarantees

#### Per-tablet ordered delivery guarantee

All changes for a row (or rows in the same tablet) will be received in the order in which they happened. However, due to the distributed nature of the problem, there is no guarantee the order across tablets.

For example, let us imagine the following scenario:

* Two rows are being updated concurrently.
* These two rows belong to different tablets.
* The first row `row #1` was updated at time `t1` and the second row `row #2` was updated at time `t2`.

In this case, it is entirely possible for the CDC feature to push the later update corresponding to `row #2` change to Kafka before pushing the update corresponding to `row #1`.

#### At-least once delivery

Updates for rows will be pushed at least once. This can happen in case of tablet leader change where the old leader already pushed changes to Kafka/Elastic Search but the latest pushed op id was not updated in the CDC metadata.

For example, let us imagine a CDC client has received changes for a row at times t1 and t3. It is possible for the client to receive those updates again.

#### No gaps in change stream

Note that once you have received a change for a row for some timestamp t, you will not receive a previously unseen change for that row at a lower timestamp. Therefore, there is a guarantee at all times that receiving any change implies all older changes have been received for a row.

[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/docdb-change-data-capture.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

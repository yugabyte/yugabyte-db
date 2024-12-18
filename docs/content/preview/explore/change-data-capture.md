---
title: Change data capture in YugabyteDB
headerTitle: Change data capture in YugabyteDB
linkTitle: Change data capture
description: Change data capture in YugabyteDB.
headcontent: Capture changes made to data in the database
tags:
  feature: early-access
menu:
  preview:
    identifier: change-data-capture
    parent: explore
    weight: 280
type: docs
---

In databases, change data capture (CDC) is a set of software design patterns used to determine and track the data that has changed so that action can be taken using the changed data. CDC is beneficial in a number of scenarios:

- **Microservice-oriented architectures**: Some microservices require a stream of changes to the data, and using CDC in YugabyteDB can provide consumable data changes to CDC subscribers.

- **Asynchronous replication to remote systems**: Remote systems may subscribe to a stream of data changes and then transform and consume the changes. Maintaining separate database instances for transactional and reporting purposes can be used to manage workload performance.

- **Multiple data center strategies**: Maintaining multiple data centers enables enterprises to provide high availability (HA).

- **Compliance and auditing**: Auditing and compliance requirements can require you to use CDC to maintain records of data changes.

YugabyteDB's CDC implementation uses [PostgreSQL Logical Replication](https://www.postgresql.org/docs/11/logical-replication.html), ensuring compatibility with PostgreSQL CDC systems.

Logical replication operates through a publish-subscribe model, where publications (source tables) send changes to subscribers (target systems).

### How it works

1. Set up publications in the YugabyteDB cluster using the same syntax as PostgreSQL.

    A publication is a set of changes generated from a table or a group of tables, and might also be described as a change set or replication slot. Each publication exists in only one database.

    You configure a replication slot to use an output plugin. YugabyteDB bundles output plugins with the standard distribution so there is always one present and no additional libraries need to be installed.

1. Deploy the YugabyteDB Connector in your preferred Kafka Connect environment.

    The YugabyteDB connector interprets the raw replication event stream directly into change events and publishes them directly to a Kafka topic.

    When the connector first connects to a YugabyteDB database, it starts by taking a consistent snapshot of all schemas. After the initial snapshot, the connector continuously captures row-level changes (inserts, updates, and deletes) committed to the YugabyteDB database.

A replication slot emits each change just once in normal operation. The current position of each slot is persisted only at checkpoint, so if a replication process is interrupted and restarts, even if the checkpoint or the starting LSN falls in the middle of a transaction, _the entire transaction is retransmitted_. This behavior guarantees that clients receive complete transactions without missing any intermediate changes, maintaining data integrity across the replication stream​. Logical decoding clients are responsible for avoiding ill effects from handling the same message more than once. Clients may wish to record the last LSN they saw when decoding and skip over any repeated data or (when using the replication protocol) request that decoding start from that LSN rather than letting the server determine the start point.

## Try it out

The following example uses a PostgreSQL database as the sink database, which will be populated using a JDBC Sink Connector.

### Prerequisites**

- [Docker](https://www.docker.com) 20 or later.
- [Docker Compose](https://docs.docker.com/compose/install/) 1.29 or later.

### YugabyteDB cluster

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local numnodes="1" rf="1" >}}

{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

You will need the IP addresses of the nodes in your cluster.

```sh
export NODE=<IP-OF-YOUR-NODE>
export MASTERS=<MASTER-ADDRESSES>
```

### Set up change data capture

1. Create a table.

    This example uses the [Retail Analytics](../../sample-data/retail-analytics/) sample dataset. All the SQL scripts are also copied in this repository for the ease of use, to create the tables in the dataset, use the following command:

    ```sql
    \i scripts/schema.sql
    ```

1. Create a stream ID using yb-admin:

    ```sh
    ./yb-admin --master_addresses $MASTERS create_change_data_stream ysql.<namespace>
    ```

1. Start the docker containers:

    ```sh
    docker-compose up -d
    ```

1. Deploy the source connector:

    ```sh
    ./deploy-sources.sh <stream-id-created-in-step-3>
    ```

1. Deploy the sink connector:

    ```sh
    ./deploy-sinks.sh
    ```

1. To sign in to the PostgreSQL terminal, run the following command:

    ```sh
    docker run --network=cdc-quickstart-kafka-connect_default -it --rm --name postgresqlterm --link pg:postgresql --rm postgres:11.2 sh -c 'PGPASSWORD=postgres exec psql -h pg -p "$POSTGRES_PORT_5432_TCP_PORT" -U postgres'
    ```

1. To perform operations and insert data to the created tables, you can use the other scripts bundled under `scripts`.

    ```sql
    \i scripts/products.sql;
    \i scripts/users.sql;
    \i scripts/orders.sql;
    \i scripts/reviews.sql;
    ```

### Monitor

#### Confluent Control Center

The Confluent Control Center UI is bundled with this example. After everything is running, use it to monitor the topics and the connect clusters, and so on. Access the controle center at <http://localhost:9021>.

#### Grafana

You can also access Grafana to view the metrics related to CDC and connectors. The dashboard is available at <http://localhost:3000>.

Use the user name `admin` and password `admin` to sign in to the console.

Use the Kafka Connect Metrics Dashboard to view the basic consolidated metrics in one place. You can start navigating around other dashboards if you need any particular metric.

## Learn more

[Change data capture](../../develop/change-data-capture/)

---
title: Get started with Flink CDC and YugabyteDB
headerTitle: Get started
linkTitle: Get started
description: Stream YugabyteDB changes to a downstream database using Apache Flink CDC.
headcontent: Stream YugabyteDB changes using Apache Flink CDC
tags:
  feature: tech-preview
menu:
  stable:
    identifier: flink-cdc-get-started
    parent: explore-change-data-capture-flink-cdc
    weight: 10
type: docs
---

This tutorial walks through deploying a YugabyteDB-to-PostgreSQL change data capture pipeline using Docker Compose and the Flink SQL Client. By the end you will have a running Flink job that captures INSERT, UPDATE, and DELETE events from a YugabyteDB table and applies them to a PostgreSQL sink table in real time.

## Prerequisites

Before you begin, ensure you have the following:

- **Apache Flink 1.20.x** cluster (validated version) with the `postgres-cdc` and JDBC sink connector JARs placed in `/opt/flink/lib/`.

    Pre-built JARs for YugabyteDB are published on the [Yugabyte Flink CDC Releases page](https://github.com/yugabyte/flink-cdc/releases). The Docker image used in this tutorial bundles them automatically.

- **YugabyteDB universe** with YSQL logical replication enabled and network connectivity to your Flink cluster.

    Note the IP address of a tserver node that is reachable from the Flink containers.

- **Docker and Docker Compose** installed and running.

- A **sink PostgreSQL database** accessible from the Flink containers.

## Step 1. Prepare YugabyteDB

Connect to your YugabyteDB cluster using `ysqlsh` and run the following SQL to create the source table, publication, and replication slot:

```sql
CREATE TABLE shipments (
  shipment_id INT PRIMARY KEY,
  order_id    INT,
  origin      TEXT,
  destination TEXT,
  is_arrived  BOOLEAN
);

INSERT INTO shipments VALUES
  (1001, 1, 'Beijing',  'Shanghai',    TRUE),
  (1002, 2, 'New York', 'Los Angeles', FALSE),
  (1003, 3, 'Mumbai',   'Delhi',       TRUE);

CREATE PUBLICATION dbz_publication FOR ALL TABLES;
SELECT * FROM pg_create_logical_replication_slot('flink', 'pgoutput');
```

{{< note title="Slot name uniqueness" >}}
Assign a unique `slot.name` to each Flink pipeline. Using the same slot name in multiple pipelines causes errors about active PIDs on the same slot.
{{< /note >}}

## Step 2. Prepare the sink database

Connect to your PostgreSQL sink database and create the target table:

```sql
CREATE TABLE shipments (
  shipment_id INT PRIMARY KEY,
  order_id    INT,
  origin      TEXT,
  destination TEXT,
  is_arrived  BOOLEAN
);
```

## Step 3. Configure Docker Compose

Create a `docker-compose.yaml` file with the following content. Replace the environment variable placeholders with your actual values.

```yaml
services:
  jobmanager:
    image: quay.io/yugabyte/ybdb-flink-cdc:fl.3.5.yb.2025.2.0-SNAPSHOT.1
    container_name: flink-jobmanager
    hostname: jobmanager
    ports:
      - "8081:8081"
      - "6123:6123"
    command: jobmanager
    volumes:
      - ./checkpoints:/opt/flink/checkpoints
    environment:
      YB_YSQL_HOST: ${YB_YSQL_HOST}
      YB_YSQL_PORT: ${YB_YSQL_PORT}
      SINK_JDBC_URL: ${SINK_JDBC_URL}
      FLINK_PROPERTIES: |-
        restart-strategy.type: fixed-delay
        restart-strategy.fixed-delay.attempts: 800
        restart-strategy.fixed-delay.delay: 15 s
        state.checkpoints.dir: file:///opt/flink/checkpoints
    extra_hosts:
      - "yb-ysql:${YB_YSQL_HOST}"

  taskmanager:
    image: quay.io/yugabyte/ybdb-flink-cdc:fl.3.5.yb.2025.2.0-SNAPSHOT.1
    container_name: flink-taskmanager
    hostname: taskmanager
    depends_on: [jobmanager]
    command: taskmanager
    volumes:
      - ./checkpoints:/opt/flink/checkpoints
    environment:
      JOB_MANAGER_RPC_ADDRESS: jobmanager
      TASK_MANAGER_NUMBER_OF_TASK_SLOTS: "4"
      YB_YSQL_HOST: ${YB_YSQL_HOST}
      YB_YSQL_PORT: ${YB_YSQL_PORT}
      SINK_JDBC_URL: ${SINK_JDBC_URL}
      FLINK_PROPERTIES: |-
        restart-strategy.type: fixed-delay
        restart-strategy.fixed-delay.attempts: 100
        restart-strategy.fixed-delay.delay: 15 s
        state.checkpoints.dir: file:///opt/flink/checkpoints
    extra_hosts:
      - "yb-ysql:${YB_YSQL_HOST}"
```

Create a `.env` file in the same directory with the following configuration variables:

```sh
YB_YSQL_HOST=<tserver-ip>
YB_YSQL_PORT=5433
SINK_JDBC_URL=jdbc:postgresql://host.docker.internal:5432/postgres?user=postgres&password=postgres
```

Start the Flink cluster:

```sh
docker compose up -d
```

Verify that both containers are running and the Flink Web UI is accessible at `http://localhost:8081`.

## Step 4. Submit the streaming job

Open the Flink SQL Client inside the jobmanager container:

```sh
docker compose exec jobmanager ./bin/sql-client.sh
```

In the SQL Client, configure the runtime and checkpointing settings, then define the source and sink tables:

```sql
SET 'execution.runtime-mode'                      = 'streaming';
SET 'execution.checkpointing.interval'            = '60 s';
SET 'execution.checkpointing.timeout'             = '10 min';

-- Source table: YugabyteDB via postgres-cdc connector
CREATE TABLE yb_shipments (
  shipment_id INT,
  order_id    INT,
  origin      STRING,
  destination STRING,
  is_arrived  BOOLEAN,
  PRIMARY KEY (shipment_id) NOT ENFORCED
) WITH (
  'connector'              = 'postgres-cdc',
  'hostname'               = '<tserver-ip>',
  'port'                   = '5433',
  'username'               = 'yugabyte',
  'password'               = 'yugabyte',
  'database-name'          = 'yugabyte',
  'schema-name'            = 'public',
  'table-name'             = 'shipments',
  'slot.name'              = 'flink',
  'decoding.plugin.name'   = 'pgoutput',
  'debezium.database.sslmode'    = 'require',
  'debezium.database.sslrootcert' = '/opt/yb-ysql-ca/ca.crt'
);

-- Sink table: PostgreSQL via JDBC connector
CREATE TABLE pg_shipments (
  shipment_id INT,
  order_id    INT,
  origin      STRING,
  destination STRING,
  is_arrived  BOOLEAN,
  PRIMARY KEY (shipment_id) NOT ENFORCED
) WITH (
  'connector'  = 'jdbc',
  'url'        = 'jdbc:postgresql://<sink-host>:5432/postgres',
  'table-name' = 'shipments',
  'username'   = 'your_user',
  'password'   = 'your_password'
);

-- Start the streaming job
INSERT INTO pg_shipments SELECT * FROM yb_shipments;
```

{{< note title="decoding.plugin.name" >}}
Always set `decoding.plugin.name` to `pgoutput`. YugabyteDB does not support the `decoderbufs` plugin that Flink CDC uses by default.
{{< /note >}}

## Step 5. Validate end-to-end propagation

After the job starts, perform some DML operations on the YugabyteDB source table using `ysqlsh` and verify that the changes are reflected in the PostgreSQL sink:

```sql
-- Insert a new shipment
INSERT INTO shipments VALUES (1004, 4, 'London', 'Paris', FALSE);

-- Update an existing shipment
UPDATE shipments SET is_arrived = TRUE WHERE shipment_id = 1002;

-- Delete a shipment
DELETE FROM shipments WHERE shipment_id = 1003;
```

Query the sink table in PostgreSQL to confirm that the changes have propagated.

Monitor the Flink job status, throughput, and checkpoint health at `http://localhost:8081`.

## Connectivity reference

| Parameter | Value |
| :--- | :--- |
| Host | tserver IP reachable from Flink |
| Port | `5433` (YSQL) |
| `decoding.plugin.name` | `pgoutput` |
| SSL mode | `require` (recommended) |

## Disable the pipeline

To stop the pipeline, cancel the Flink job from the Web UI at `http://localhost:8081` or by running:

```sh
docker compose exec jobmanager ./bin/flink cancel <job-id>
```

To release the replication slot and publication, run the following in `ysqlsh`:

```sql
SELECT pg_drop_replication_slot('flink');
DROP PUBLICATION dbz_publication;
```

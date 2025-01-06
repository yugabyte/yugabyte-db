---
title: Change data capture in YugabyteDB
headerTitle: Change data capture
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

In databases, change data capture (CDC) is a set of software design patterns used to determine and track the data that has changed so that action can be taken using the changed data. CDC can be used for a number of scenarios:

- Microservice-oriented architectures - Some microservices require a stream of changes to the data, and using CDC in YugabyteDB can provide consumable data changes to CDC subscribers.

- Asynchronous replication to remote systems - Remote systems may subscribe to a stream of data changes and then transform and consume the changes. Maintaining separate database instances for transactional and reporting purposes can be used to manage workload performance.

- Multiple data center strategies - Maintaining multiple data centers enables enterprises to provide high availability (HA).

- Compliance and auditing - Satisfy auditing and compliance requirements using CDC to maintain records of data changes.

YugabyteDB's CDC implementation uses [PostgreSQL Logical Replication](https://www.postgresql.org/docs/11/logical-replication.html), ensuring compatibility with PostgreSQL CDC systems. Logical replication operates through a publish-subscribe model, where publications (source tables) send changes to subscribers (target systems).

CDC via logical replication is supported in YugabyteDB starting from v2024.1.1.

## How it works

YugabyteDB logical replication can be used in conjunction with Apache Kafka to create a scalable, fault-tolerant, and highly available data pipeline as follows:

1. Logical replication: YugabyteDB publishes changes to a logical replication slot, which captures the changes in a format that can be consumed by Kafka.
1. [YugabyteDB Connector](../../develop/change-data-capture/using-logical-replication/yugabytedb-connector/): The logical replication slot is connected to the YugabyteDB Connector, a Kafka Connect worker based on Debezium, which converts the PostgreSQL change stream into Kafka messages.
1. Kafka topics: YugabyteDB Connector publishes the messages to one or more Kafka topics.
1. Kafka consumers: Kafka consumers subscribe to the topics and process the messages, which can be used for further processing, storage, or analysis.

Using Kafka with PostgreSQL logical replication provides several benefits:

- Scalability: Kafka can handle high volumes of messages, making it an ideal choice for large-scale data pipelines.
- Fault tolerance: Kafka provides built-in fault tolerance, ensuring that messages are not lost in case of failures.
- High availability: Kafka can handle high availability use cases, such as streaming data from multiple PostgreSQL instances.

## Try it out

The following example uses [pg_recvlogical](https://www.postgresql.org/docs/current/app-pgrecvlogical.html), a command-line tool provided by PostgreSQL for interacting with the logical replication feature. It is specifically used to receive changes from the database using logical replication slots.

YugabyteDB provides the pg_recvlogical binary in the `<yugabyte-db-dir>/postgres/bin/` directory, which is inherited from and based on PostgreSQL 11.2. Although PostgreSQL also offers a pg_recvlogical binary, you should use the YugabyteDB version to avoid compatibility issues.

### Set up pg_recvlogical

To set up pg_recvlogical, create and start the local cluster by running the following command from your YugabyteDB home directory:

```sh
./bin/yugabyted start \
  --advertise_address=127.0.0.1 \
  --base_dir="${HOME}/var/node1" \
  --tserver_flags="allowed_preview_flags_csv={cdcsdk_enable_dynamic_table_support},cdcsdk_enable_dynamic_table_support=true,cdcsdk_publication_list_refresh_interval_secs=2"
```

### Create tables

1. Use ysqlsh to connect to the default `yugabyte` database with the default superuser `yugabyte`, as follows:

    ```sh
    ./bin/ysqlsh -h 127.0.0.1 -U yugabyte -d yugabyte
    ```

1. In the `yugabyte` database, create a table `employees`.

    ```sql
    CREATE TABLE employees (
        employee_id SERIAL PRIMARY KEY,
        name VARCHAR(255),
        email VARCHAR(255),
        department_id INTEGER
    );
    ```

### Create a replication slot

Create a logical replication slot named `test_logical_replication_slot` using the `test_decoding` output plugin via the following function:

```sql
SELECT * FROM pg_create_logical_replication_slot('test_logical_replication_slot', 'test_decoding');
```

```output
           slot_name           | lsn
-------------------------------+-----
 test_logical_replication_slot | 0/2
```

### Configure and start pg_recvlogical

The pg_recvlogical binary can be found under `<yugabyte-db-dir>/postgres/bin/`.

Open a new shell and start pg_recvlogical to connect to the `yugabyte` database with the superuser `yugabyte` and replicate changes using the replication slot you created as follows:

```sh
./pg_recvlogical -d yugabyte \
  -U yugabyte \
  -h 127.0.0.1 \
  --slot test_logical_replication_slot \
  --start \
  -f -
```

Any changes that get replicated are printed to stdout.

For more information about pg_recvlogical configuration, refer to the PostgreSQL [pg_recvlogical](https://www.postgresql.org/docs/11/app-pgrecvlogical.html) documentation.

### Verify replication

Return to the shell where ysqlsh is running. Perform DMLs on the `employees` table.

```sql
BEGIN;

INSERT INTO employees (name, email, department_id)
VALUES ('Alice Johnson', 'alice@example.com', 1);

INSERT INTO employees (name, email, department_id)
VALUES ('Bob Smith', 'bob@example.com', 2);

COMMIT;
```

Expected output observed on stdout where pg_recvlogical is running:

```output
BEGIN 2
table public.employees: INSERT: employee_id[integer]:1 name[character varying]:'Alice Johnson' email[character varying]:'alice@example.com' department_id[integer]:1
table public.employees: INSERT: employee_id[integer]:2 name[character varying]:'Bob Smith' email[character varying]:'bob@example.com' department_id[integer]:2
COMMIT 2
```

### Add tables

You can add a new table to the `yugabyte` database and any DMLs performed on the new table are also replicated to pg_recvlogical.

1. In the `yugabyte` database, create a new table `projects`:

    ```sql
    CREATE TABLE projects (
      project_id SERIAL PRIMARY KEY,
      name VARCHAR(255),
      description TEXT
    );
    ```

2. Perform DMLs on the `projects` table:

    ```sql
    INSERT INTO projects (name, description)
    VALUES ('Project A', 'Description of Project A');
    ```

Expected output observed on stdout where pg_recvlogical is running:

```output
BEGIN 3
table public.projects: INSERT: project_id[integer]:1 name[character varying]:'Project A' description[text]:'Description of Project A'
COMMIT 3
```

{{% explore-cleanup-local %}}

## Learn more

- [Change data capture](../../develop/change-data-capture/)
- [Get started with YugabyteDB Connector](../../develop/change-data-capture/using-logical-replication/get-started/)

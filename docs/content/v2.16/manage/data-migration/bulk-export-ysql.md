---
title: Bulk export
headerTitle: Bulk export for YSQL
linkTitle: Bulk export
description: Bulk export for YSQL using ysql_dump.
menu:
  v2.16:
    identifier: manage-bulk-export-ysql
    parent: manage-bulk-import-export
    weight: 704
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
   <li >
    <a href="../bulk-export-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../bulk-export-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

This page describes the following steps required to export PostgreSQL data to YugabyteDB:

- [Convert a PostgreSQL schema](#convert-a-postgresql-schema)
- [Migrate a PostgreSQL application](#migrate-a-postgresql-application)
- [Export PostgreSQL data](#export-postgresql-data)

## Convert a PostgreSQL schema

To convert the PostgreSQL schema to YugabyteDB schema, the following changes need to be made.

{{< tip title="Tip" >}}

The `ysql_dump` tool can simplify some steps of your schema migration, refer to [use ysql_dump](#use-ysql-dump).

{{< /tip >}}

### Specify `PRIMARY KEY` inline

YugabyteDB supports the PostgreSQL syntax of first declaring a table, and subsequently running an ALTER TABLE command to add the primary key. Note that the ALTER TABLE operation requires a disk re-write and may be resource intensive, so it is recommended to set the primary key inline as part of the CREATE TABLE operation.

### Use `HASH` sort order

In YugabyteDB, the sort order of the primary key (or index) of a table determines the data distribution strategy for that primary key (or index) table across the nodes of a cluster. Thus, the choice of the sort order is critical in determining the data distribution strategy.

Indexes using the `ASC` or `DESC` sort order can efficiently handle both point and range lookups. However, they will start off with a single tablet, and therefore all reads and writes to this table will be handled by a single tablet initially. The tablet would need to undergo dynamic splitting for the table to leverage multiple nodes. Creating `ASC` or `DESC` sort orders for large datasets when range queries are not required could result in a hot shard problem.

To overcome the above issues, YugabyteDB supports `HASH` ordering in addition to the standard `ASC` and `DESC` sort orders for indexes. With HASH ordering, a hash value is first computed by applying a hash function to the values of the corresponding columns, and the hash value is sorted. Because the sort order is effectively random, this results in a random distribution of data across the various nodes in the cluster. Random distribution of data has the following properties:

- It can eliminate hot spots in the cluster by evenly distributing data across all nodes.
- The table can be pre-split to utilize all nodes of a cluster right from the start.
- Range queries **cannot** be efficiently supported on the index.

<!-- Commenting out this section which can be introduced again when we can recommend colocation.
 ### Optimize databases with many objects

{{< tip title="Tip" >}}

Databases with over 500 objects (tables, indexes and unique constraints mainly) would benefit from the colocation optimization here. Colocation also improves join performance for smaller tables.

{{< /tip >}}

In many scenarios, there may be a large number of database objects (tables and indexes specifically) which hold a relatively small dataset. In such cases, creating a separate tablet for each table and index could drastically reduce performance. Colocating these tables and indexes into a single tablet can drastically improve performance.

Enabling the colocation property at a database level causes all tables created in this database to be colocated by default. Tables in this database that hold a large dataset or those that are expected to grow in size over time can be opted out of the colocation group, which would cause them to be split into multiple tablets.

{{< note title="Note" >}}

Making colocation the default for all databases is [work in progress](https://github.com/yugabyte/yugabyte-db/issues/5239).

{{< /note >}} -->

### Pre-split large tables

For larger tables and indexes that are hash-sharded, specify the number of initial tablet splits desired as part of the DDL statement of the table. This can be very beneficial to distribute the data of the table across multiple nodes right from the start. Refer to [Hash-sharded tables](../../../architecture/docdb-sharding/tablet-splitting/#hash-sharded-tables) for an example on specifying the number of tablets at table creation time.

For larger tables and indexes that are range-sharded and the value ranges of the primary key columns are known ahead of time, pre-split them at the time of creation. This is especially beneficial for range sharded tables/indexes. Refer to [range-sharded-tables](../../../architecture/docdb-sharding/tablet-splitting/#range-sharded-tables) for syntax of pre-splitting an index.

### Remove collation on columns

Remove the COLLATE options in order move the schema over to YugabyteDB. Refer to [Collations](../../../explore/ysql-language-features/advanced-features/collations/) to learn more.

For example, consider the following table definition.

```plpgsql
CREATE TABLE test1 (
    a text COLLATE "de_DE" PRIMARY KEY,
    b text COLLATE "es_ES"
);
```

Attempting to create this table would result in the following error.

```output
ERROR:  0A000: COLLATE not supported yet
LINE 2:     a text COLLATE "de_DE" PRIMARY KEY,
                   ^
HINT:  See https://github.com/YugaByte/yugabyte-db/issues/1127. Click '+' on the description to raise its priority
LOCATION:  raise_feature_not_supported_signal, gram.y:17113
Time: 31.543 ms
```

The COLLATE options should be dropped as follows:

```plpgsql
CREATE TABLE test1 (
    a text PRIMARY KEY,
    b text
);
```

### Optimize sequences (SERIAL)

All sequences in your schema currently use a default `CACHE` value of 1. In a distributed DB, this will result in each `INSERT` performing extra RPC calls to generate new row IDs, dramatically reducing write performance.

Consider the following table as an example.

```plpgsql
CREATE TABLE contacts (
  contact_id SERIAL,
  first_name VARCHAR NOT NULL,
  last_name VARCHAR NOT NULL,
  email VARCHAR NOT NULL,
  phone VARCHAR,
  PRIMARY KEY (contact_id)
);
```

One of the following techniques is recommended (in the order of preference) to improve performance when using sequences.

#### Option 1. Larger `CACHE` value for `SERIAL`

In order to use the `SERIAL` data type and not incur a performance penalty on `INSERT` operations, setting the cache size to 1000 is recommended. This can be achieved in the example table above by running an `ALTER` command on the sequence in the following manner.

```sql
ALTER SEQUENCE contacts_contact_id_seq CACHE 1000;
```

You can find the name of the sequence as follows:

```output.sql
yugabyte=# SELECT pg_get_serial_sequence('contacts', 'contact_id');
     pg_get_serial_sequence
--------------------------------
 public.contacts_contact_id_seq
(1 row)
```

#### Option 2. Use `UUID`s instead of `SERIAL`

The recommended option is to use UUIDs instead of the SERIAL data type. UUIDs are globally unique identifiers that can be generated on any node without requiring any global inter-node coordination.

Some systems refer to this data type as a globally unique identifier, or GUID, instead.

A UUID is a 128-bit quantity that is generated by an algorithm chosen to make it very unlikely that the same identifier will be generated by anyone else in the known universe using the same algorithm. Therefore, for distributed systems, these identifiers provide a better uniqueness guarantee than sequence generators, which are only unique in a single database.

The table shown in the previous example should be changed as follows:

```plpgsql
CREATE EXTENSION IF NOT EXISTS pgcrypto;

CREATE TABLE contacts (
  contact_id uuid DEFAULT gen_random_uuid(),
  first_name VARCHAR NOT NULL,
  last_name VARCHAR NOT NULL,
  email VARCHAR NOT NULL,
  phone VARCHAR,
  PRIMARY KEY (contact_id)
);
```

### Use `ysql_dump`

The PostgreSQL utility `pg_dump` can be used to dump the schema of a database, as described in the preceding sections of this document.

The [`ysql_dump`](../../../admin/ysql-dump/) tool (a YugabyteDB-specific version of the `pg_dump` tool) can connect to an existing PostgreSQL database and export a YugabyteDB-friendly version of the schema and, therefore, includes some of the schema modifications. Other manual changes might be required depending on the use case.

Note that `ysql_dump` has been tested with PostgreSQL versions up to 11.2 and might not work on newer versions of PostgreSQL.

## Migrate a PostgreSQL application

This section outlines the recommended changes for porting an existing PostgreSQL application to YugabyteDB.

### Retry transactions on conflicts

Only a subset of transactions that get aborted due to an internal conflict are retried transparently. YugabyteDB uses the error code 40001 (serialization_failure) for retryable transaction conflict errors. We recommend retrying the transactions from the application upon encountering these errors.

{{< note title="Note" >}}
This is the state as of YugabyteDB v2.2, reducing transaction conflicts by transparently handling retries of most transactions transparently is work in progress.
{{< /note >}}

### Distribute load evenly across the cluster

All nodes (YB-TServers) in the cluster are identical and are capable of handling queries. However, the client drivers of PostgreSQL are designed to communicate only with a single endpoint (node). In order to utilize all the nodes of the cluster evenly, the queries from the application would need to be distributed uniformly across all nodes of the cluster. There are two ways to accomplish this:

- **Use a load balancer** to front all the nodes of the cluster. The load balancer should be set to round-robin all requests across the nodes in the cluster.

- **Modify the application to distribute queries across nodes** in the cluster. In this scenario, typically a DNS entry is used to maintain the list of nodes in the cluster. The application periodically refreshes this list, and distributes the queries across the various nodes of the cluster in a round robin manner.

### Handling large number of connections

There are many applications where handling a large number of client connections is critical. There are two strategies to deal with this:

- **Evenly distribute queries across nodes:** Every node (YB-Tserver process) of a YugabyteDB cluster has a limit on the number of connections it can handle, by default this number is 300 connections. While this number can be increased a bit depending on the use case, it is recommended to distribute the queries across the different nodes in the cluster. As an example, a 10 node cluster consisting of 16 vCPU per node can handle 3000 connections.

- **Use a connection pool:** Use a connection pool in your application such as the Hikari pool. Using a connection pool drastically reduces the number of connections by multiplexing a large number of logical client connections onto a smaller number of physical connections across the nodes of the YugabyteDB cluster.

- **Increase number of nodes in cluster:**  Note that the number of connections to a YugabyteDB cluster scales linearly with the number of nodes in the cluster. By deploying more nodes with smaller vCPUs per node, it may be possible to get more connections. As an example, a 10 node cluster consisting of 32 vCPU per node can handle 3000 connections. If more connections are desirable, deploying a 20 node cluster with 16 vCPUs per node (which is equivalent to the 10 node, 32 vCPU cluster) can handle 6000 connections.

### Use PREPARED statements

Prepared statements are critical to achieve good performance in YugabyteDB because they avoid re-parsing (and typically re-planning) on every query. Most SQL drivers will auto-prepare statements, in these cases, it may not be necessary to explicitly prepare statements.

In cases when the driver does not auto-prepare, use an explicit prepared statement where possible. This can be done programmatically in the case of many drivers. In scenarios where the driver does not have support for preparing statements (for example, the Python psycopg2 driver), the queries can be optimized on each server by using the PREPARE <plan> AS <plan name> feature.

For example, if you have two tables t1 and t2 both with two columns k (primary key) and v:

```sql
CREATE TABLE t1 (k VARCHAR PRIMARY KEY, v VARCHAR);

CREATE TABLE t2 (k VARCHAR PRIMARY KEY, v VARCHAR);
```

Now, consider the following code snippet which repeatedly makes SELECT queries that are not prepared.

```python
for idx in range(num_rows):
  cur.execute("SELECT * from t1, t2 " +
              "  WHERE t1.k = t2.k AND t1.v = %s LIMIT 1"
              , ("k1"))
```

Because the Python psycopg2 driver does not support prepared bind statements (using a cursor.prepare() API), the explicit PREPARE statement is used. The above code snippet can be optimized by changing the above query to the following equivalent query.

```python
cur.execute("PREPARE myplan as " +
            "  SELECT * from t1, t2 " +
            "  WHERE t1.k = t2.k AND t1.v = $1 LIMIT 1")
  for idx in range(num_rows):
    cur.execute("""EXECUTE myplan(%s)""" % "'foo'")
```

## Export PostgreSQL data

The recommended way to export data from PostgreSQL for purposes of importing it to YugabyteDB is via CSV files using the COPY command.
However, for exporting an entire database that consists of smaller datasets, you can use the YugabyteDB [`ysql_dump`](../../../admin/ysql-dump/) utility.

{{< tip title="Migrate using YugabyteDB Voyager" >}}
To automate your migration from PostgreSQL to YugabyteDB, use [YugabyteDB Voyager](/preview/yugabyte-voyager/). To learn more, refer to the [export schema](/preview/yugabyte-voyager/migrate-steps/#export-and-analyze-schema) and [export data](/preview/yugabyte-voyager/migrate-steps/#export-data) steps.
{{< /tip >}}

### Export data into CSV files using the COPY command

To export the data, connect to the source PostgreSQL database using the psql tool, and execute the COPY TO command as follows:

```sql
COPY <table_name>
    TO '<table_name>.csv'
    WITH (FORMAT CSV, HEADER false, DELIMITER ',');
```

{{< note title="Note" >}}

The COPY TO command exports a single table, so you should execute it for every table that you want to export.

{{< /note >}}

It is also possible to export a subset of rows based on a condition:

```sql
COPY (
    SELECT * FROM <table_name>
    WHERE <condition>
)
TO '<table_name>.csv'
WITH (FORMAT CSV, HEADER false, DELIMITER ',');
```

For all available options provided by the COPY TO command, refer to the [PostgreSQL documentation](https://www.postgresql.org/docs/current/sql-copy.html).

#### Parallelize large table export

For large tables, it might be beneficial to parallelize the process by exporting data in chunks as follows:

```sql
COPY (
    SELECT * FROM <table_name>
    ORDER BY <primary_key_col>
    LIMIT num_rows_per_export OFFSET 0
)
TO '<table_name>_1.csv'
WITH (FORMAT CSV, HEADER false, DELIMITER ',');
```

```sql
COPY (
    SELECT * FROM <table_name>
    ORDER BY <primary_key_col>
    LIMIT num_rows_per_export OFFSET num_rows_per_export
)
TO '<table_name>_2.csv'
WITH (FORMAT CSV, HEADER false, DELIMITER ',');
```

```sql
COPY (
    SELECT * FROM <table_name>
    ORDER BY <primary_key_col>
    LIMIT num_rows_per_export OFFSET num_rows_per_export * 2
)
TO '<table_name>_3.csv'
WITH (FORMAT CSV, HEADER false, DELIMITER ',');
```

You can run the above commands in parallel to speed up the process. This approach will also produce multiple CSV files, allowing for parallel import on the YugabyteDB side.

### Export data into SQL script using ysql_dump

An alternative way to export the data is using the YugabyteDB [`ysql_dump`](../../../admin/ysql-dump/) backup utility, which is derived from PostgreSQL pg_dump.

```sh
$ ysql_dump -d <database_name> > <database_name>.sql
```

`ysql_dump` is the ideal option for smaller datasets, because it allows you to export a whole database by running a single command. However, the COPY command is recommended for large databases, because it significantly enhances the performance.

## Next step

- [Bulk import](../bulk-import-ysql/)

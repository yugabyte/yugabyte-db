---
title: Colocated tables
headerTitle: Colocated tables
linkTitle: Colocated tables
description: Learn how colocated tables aggregate data into a single tablet.
aliases:
  - /preview/architecture/docdb/colocated_tables/
menu:
  preview:
    identifier: docdb-colocated-tables
    parent: architecture-docdb-sharding
    weight: 1144
rightNav:
  hideH4: true
type: docs
---

YugabyteDB supports colocating SQL tables. This allows for closely related data in colocated tables to reside together in a single parent tablet called the "colocation tablet." Colocation helps to optimize for low-latency, high-performance data access by reducing the need for additional trips across the network. It also reduces the overhead of creating a tablet for every relation (tables, indexes, and so on) and the storage for these per node.

Note that all the data in the colocation tablet is still replicated across nodes in accordance with the replication factor of the cluster.

## Benefits of colocation and considerations by use case

Colocation can make sense for high-performance, real-time data processing, where low-latency and fast access to data are critical. Colocation has the following benefits:

- Improved performance and scalability: Using a single tablet instead of creating a tablet per relation reduces storage and compute overhead.
- Faster access to data: By having all the data for a single database from multiple tables stored in a single tablet, you can avoid the overhead of inter-node communication and data shuffling, which can result in faster access to data. For example, the speed of joins improves when data across the various colocated tables is local and you no longer have to read data over the network.

### When to use colocation

The decision to use colocating clusters should be based on the specific requirements of your use case, including the expected performance, data size, availability, and durability requirements. The following scenarios may benefit from colocation:

#### Small datasets needing HA or geo-distribution

Applications with smaller sized datasets may have the following pattern and requirements:

- The size of the entire dataset is small. Typically, this entire database is less than 50 GB in size.
- They require a large number of tables, indexes and other relations created in a single database.
- Need high availability and/or geographic data distribution.
- Scaling the dataset or the number of IOPS is not an immediate concern.

In this scenario, it is undesirable to have the small dataset spread across multiple nodes as this may affect the performance of certain queries due to more network hops (for example, joins).

#### Large datasets - a few large tables with many small tables

Applications that have a large dataset could have the following characteristics:

- A large number of tables and indexes.
- A handful of tables that are expected to grow large, and thereby need to be scaled out.
- The remaining tables continue to remain small.

Here, only the few large tables need to be sharded and scaled out. All other tables benefit from colocation as queries involving these tables do not need network hops.

#### Scaling the number of databases, each database with a small dataset

In some scenarios, the number of databases in a cluster grows rapidly, while the size of each database stays small. This is characteristic of a microservices-oriented architecture, where each microservice needs its own database. An example for this would be a multi-tenant SaaS service in which a database is created per customer. The net result is a lot of small databases, with the need to scale the number of databases hosted. Colocated tables allow for the entire dataset in each database to be hosted in one tablet, enabling scalability of the number of databases in a cluster by adding more nodes.

Colocating all data in a single tablet comes with some trade-offs. It can lead to a potential bottleneck in terms of resource utilization. Ultimately, the size of the dataset is just one factor to consider when determining whether a collocated database is a good fit for your use case.

## Enable colocation

Colocation can be enabled at the cluster, database, or table level. For a colocated cluster, all the databases created in the cluster will have colocation enabled by default. You can also choose to configure a single database as colocated to ensure that all the data in the database tables is stored on the single colocation tablet on a node. This can be especially helpful when working with real-time data processing or when querying large amounts of data.

### Clusters

To enable colocation for all databases in a cluster, when you create the cluster, set the following [flag](../../../reference/configuration/yb-master/#ysql-colocate-database-by-default) to true for [YB-Master](../../concepts/yb-master/) and [YB-TServer](../../concepts/yb-tserver/) services as follows:

```sql
ysql_colocate_database_by_default = true
```

You can also set this flag after creating the cluster, but you will need to restart the YB-Masters and YB-TServers.

Note: For YugabyteDB Managed, you currently cannot enable colocation for a cluster. Enable colocation for [individual databases](#databases).

### Databases

You can create a colocated database in a non-colocated cluster. Tables created in this database are colocated by default. That is, all the tables in the database share a single tablet. To enable this, at database creation time run the following command:

```sql
CREATE DATABASE <name> with COLOCATION = true
```

For a colocation-enabled cluster, you can choose to opt a specific database out of colocation using the following syntax:

```sql
CREATE DATABASE <name> with COLOCATION = false
```

{{< warning title="Deprecated syntax" >}}

The following syntax to create colocated databases is deprecated in v2.18 and later:

```sql
CREATE DATABASE <name> WITH colocated = <true|false>
```

You can create a backup of a database that was colocated with the deprecated syntax and restore it in a colocated cluster to recreate it with the new colocation implementation. You can't upgrade a database created with the deprecated syntax.

{{< /warning >}}

### Tables

All the tables in a colocated database are colocated by default. There is no need to enable colocation when creating tables. You can choose to opt specific tables out of colocation in a colocated database. To do this, use the following command:

```sql
CREATE TABLE <name> (columns) WITH (COLOCATION = false);
```

Note that you cannot create a colocated table in a non-colocated database.

{{< warning title="Deprecated syntax" >}}

The following syntax to create colocated tables is deprecated in v2.18 and later:

```sql
CREATE TABLE <name> (columns) WITH (colocated = <true|false>)
```

{{< /warning >}}

#### Change table colocation

To remove a single table from a colocation (for example, if it increases beyond a certain size), you can create a copy of the table using `CREATE TABLE AS SELECT` with colocation set to false. Do the following:

1. Rename your colocated table to ensure no further changes modify the table or its contents.
1. Create a new non-colocated table from the original colocated table using `CREATE TABLE AS SELECT`. You can choose to use the same name as the original table.
1. Optionally, drop the original colocated table after confirming reads and writes on the new, non-colocated table.

You can use the same process to add a non-colocated table to colocation in a colocated database.

{{< note title="Note" >}}

Changing table colocation requires some downtime during the creation of the new table. The time taken for this process depends on the size of the table whose colocation is changed.

{{< /note >}}

## Metrics and views

To view metrics such as table size, use the name of the parent colocation table. The colocation table name is in the format `<colocation table object ID>.colocation.parent.tablename`. All the tables in a colocation share the same metric values and these show under the colocation table for each metric. Table and tablet metrics are available at the YB-TServer endpoint (`<node-ip>:9000`) as well as in YugabyteDB Anywhere in the Metrics section for each Universe.

## Limitations and considerations

- Creating colocated tables with tablespaces is disallowed. This will be supported in future releases.
- Metrics for table metrics such as table size are available for the colocation tablet, not for individual colocated tables that are part of the colocation.
- Tablet splitting is disabled for colocated tables.
- You can't configure xCluster replication for colocated tables using the YugabyteDB Anywhere UI in the 2.18.0 release. This functionality will be available in a future release.

### Semantic differences between colocated and non-colocated tables

Concurrent DML and DDL on different tables in the same colocated database will abort the DML. This is not the case for distributed, non-colocated tables.
For a colocated table, a TRUNCATE / DROP operation may abort due to conflicts if another session is holding row-level locks on the table.

## xCluster and colocation

xCluster is supported for colocated tables in v2.18.0 only via [yb-admin](../../../admin/yb-admin/). To set up xCluster for colocated tables, the `colocationid` for a given table needs to match on the source and target universes.

To set up xCluster for colocated tables, do the following:

1. Create the table in the colocated database on the source universe with colocation ID explicitly specified.

    ```SQL
    CREATE TABLE <name> WITH (COLOCATION = true, COLOCATION_ID = 20000)
    ```

1. Create the table in the colocated database on the target universe using the same colocation ID.

    ```SQL
    CREATE TABLE <name> WITH (COLOCATION = true, COLOCATION_ID = 20000)
    ```

1. Get the parent table UUID for the colocated database.

    ```sh
    ./yb-admin -master_addresses <source_master_addresses> list_tables include_table_id | grep -i <database_name> | grep -i "colocation.parent.uuid"
    ```

    ```output
    col_db.00004000000030008000000000004004.colocation.parent.tablename 00004000000030008000000000004004.colocation.parent.uuid
    ```

1. Set up replication for the parent colocation table using yb-admin.

    ```sh
    ./yb-admin -master_addresses <target_master_addresses> setup_universe_replication <replication_group_name> <source_master_addresses> <parent_colocated_table_uuid>
    ```

    For example:

    ```sh
    ./yb-admin -master_addresses 127.0.0.2 setup_universe_replication A1-B2 127.0.0.1 00004000000030008000000000004004.colocation.parent.uuid
    ```

    ```output
    Replication setup successfully
    ```

If new colocated tables are added to the same colocated database on both source and target universes with matching colocation IDs, then they are automatically included in replication.

For information on how to set up xCluster for non-colocated tables, refer to [xCluster deployment](../../../deploy/multi-dc/async-replication/).

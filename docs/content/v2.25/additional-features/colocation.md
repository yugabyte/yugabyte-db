---
title: Colocating tables and databases
headerTitle: Colocating tables
linkTitle: Colocation
description: Learn how colocated tables aggregate data into a single tablet.
menu:
  v2.25:
    identifier: colocation
    parent: additional-features
    weight: 50
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

In this case, only the few large tables need to be sharded and scaled out. All other tables benefit from colocation as queries involving these tables do not need network hops.

#### Scaling the number of databases, each database with a small dataset

In some scenarios, the number of databases in a cluster grows rapidly, while the size of each database stays small. This is characteristic of a microservices-oriented architecture, where each microservice needs its own database. An example for this would be a multi-tenant SaaS service in which a database is created per customer. The net result is a lot of small databases, with the need to scale the number of databases hosted. Colocated tables allow for the entire dataset in each database to be hosted in one tablet, enabling scalability of the number of databases in a cluster by adding more nodes.

Colocating all data in a single tablet comes with some trade-offs. It can lead to a potential bottleneck in terms of resource utilization. Ultimately, the size of the dataset is just one factor to consider when determining whether a collocated database is a good fit for your use case.

## Enable colocation

Colocation can be enabled at the cluster, database, or table level. For a colocated cluster, all the databases created in the cluster will have colocation enabled by default. You can also choose to configure a single database as colocated to ensure that all the data in the database tables is stored on the single colocation tablet on a node. This can be especially helpful when working with real-time data processing or when querying large amounts of data.

### Clusters

To enable colocation for all databases in a cluster, when you create the cluster, set the following [flag](../../reference/configuration/yb-master/#ysql-colocate-database-by-default) to true for [YB-Master](../../architecture/yb-master/) and [YB-TServer](../../architecture/yb-tserver/) services as follows:

```sql
ysql_colocate_database_by_default = true
```

You can also set this flag after creating the cluster, but you will need to restart the YB-Masters and YB-TServers.

Note: For YugabyteDB Aeon, you currently cannot enable colocation for a cluster. Enable colocation for [individual databases](#databases).

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

To check if a database is colocated or not, you can use the `yb_is_database_colocated` function as follows:

```sql
select yb_is_database_colocated();
```

You should see an output similar to the following:

```output
 yb_is_database_colocated
--------------------------
 t
```

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

To check if a table is colocated or not, you can use the [\d](../../api/ysqlsh-meta-commands/#d-s-pattern-patterns) meta-command in [ysqlsh](../../api/ysqlsh/). You can also retrieve the same information using the `yb_table_properties()` function as follows:

```sql
select is_colocated from yb_table_properties('table_name'::regclass);
```

You should see an output similar to the following:

```output
 is_colocated
--------------
 f
```

#### Change table colocation

To remove a single table from a colocation (for example, if it increases beyond a certain size), you can create a copy of the table using CREATE TABLE AS SELECT with colocation set to false. Do the following:

1. Rename your colocated table to ensure no further changes modify the table or its contents.
1. Create a new non-colocated table from the original colocated table using CREATE TABLE AS SELECT. You can choose to use the same name as the original table.
1. Optionally, drop the original colocated table after confirming reads and writes on the new, non-colocated table.

You can use the same process to add a non-colocated table to colocation in a colocated database.

{{< note title="Note" >}}

Changing table colocation requires some downtime during the creation of the new table. The time taken for this process depends on the size of the table whose colocation is changed.

{{< /note >}}

## Metrics and views

To view metrics such as table size, use the name of the parent colocation table. The colocation table name is in the format `<colocation table object ID>.colocation.parent.tablename`. All the tables in a colocation share the same metric values and these show under the colocation table for each metric. Table and tablet metrics are available at the YB-TServer endpoint (`<node-ip>:9000`) as well as in YugabyteDB Anywhere in the Metrics section for each Universe.

## Limitations and considerations

- Metrics for table metrics such as table size are available for the colocation tablet, not for individual colocated tables that are part of the colocation.
- Tablet splitting is disabled for colocated tables.
- To avoid hotspots, do not colocate tables that receive disproportionately high loads.
- xCluster replication automatic mode does not yet support colocated tables.

### Semantic differences between colocated and non-colocated tables

Concurrent DML and DDL on different tables in the same colocated database will abort the DML. This is not the case for distributed, non-colocated tables.

For a colocated table, a TRUNCATE / DROP operation may abort due to conflicts if another session is holding row-level locks on the table.

## xCluster and colocation

xCluster replication currently only supports colocated tables for [semi-automatic and fully manual](../../deploy/multi-dc/async-replication/async-transactional-setup-semi-automatic/) modes.

When setting up xCluster for colocated tables when using manual or
semi-automatic mode, the `colocation_id` for a given table or index
needs to match on the source and target universes.

To set up xCluster for colocated tables, do the following:

1. Create the table in the colocated database on the source universe with colocation ID explicitly specified.

    ```SQL
    CREATE TABLE <name> WITH (COLOCATION = true, COLOCATION_ID = 20000)
    ```

1. Create the table in the colocated database on the target universe using the same colocation ID.

    ```SQL
    CREATE TABLE <name> WITH (COLOCATION = true, COLOCATION_ID = 20000)
    ```

1. Create the index in the colocated database on the source universe with colocation ID explicitly specified.

    ```SQL
    CREATE INDEX <index_name> ON TABLE <table_name> WITH (COLOCATION_ID = 20000)
    ```

1. Create the index in the colocated database on the target universe using the same colocation ID.

    ```SQL
    CREATE INDEX <index_name> ON TABLE <table_name> WITH (COLOCATION_ID = 20000)
    ```

1. Get the parent table UUID for the colocated database.

    ```sh
    ./yb-admin --master_addresses <source_master_addresses> list_tables include_table_id | grep -i <database_name> | grep -i "colocation.parent.uuid"
    ```

    ```output
    col_db.00004000000030008000000000004004.colocation.parent.tablename 00004000000030008000000000004004.colocation.parent.uuid
    ```

1. Set up replication for the parent colocation table using yb-admin.

    ```sh
    ./yb-admin --master_addresses <target_master_addresses> setup_universe_replication <replication_group_name> <source_master_addresses> <parent_colocated_table_uuid>
    ```

    For example:

    ```sh
    ./yb-admin --master_addresses 127.0.0.2 setup_universe_replication A1-B2 127.0.0.1 00004000000030008000000000004004.colocation.parent.uuid
    ```

    ```output
    Replication setup successfully
    ```

If more colocated tables are added to the same colocated database on
both source and target universes with matching colocation IDs, then they
are automatically included in replication.  There is no need to set up
the parent table for replication again.

For information on how to set up xCluster for non-colocated tables, refer to [xCluster deployment](../../deploy/multi-dc/async-replication/).

## Colocated tables with tablespaces

{{<tags/feature/ea idea="1104">}}Colocated tables can be placed in [tablespaces](../../explore/going-beyond-sql/tablespaces/). When a colocated table is created in a tablespace, the colocation tablet is placed and replicated exclusively in the tablespace.

During Early Access, by default colocation support for tablespaces is not enabled. To enable the feature, set the flag `ysql_enable_colocated_tables_with_tablespaces=true`.

### Create a colocated table in a tablespace

In a [colocated database](#databases), [tables](#tables) are created with colocation by default. To create a colocated table in a tablespace, use the following command:

```sql
CREATE TABLE <table_name> TABLESPACE <tablespace_name>;
```

Use the same syntax to create colocated indexes and materialized views in a tablespace as follows:

```sql
CREATE INDEX <index_name> ON <table_name>(<column_name>) TABLESPACE <tablespace_name>;
CREATE MATERIALIZED VIEW <view_name> TABLESPACE <tablespace_name> AS <query>;
```

To create a non-colocated table (in a colocated database) in a tablespace, use the following command:

```sql
CREATE TABLE <table_name> WITH (COLOCATION=FALSE) TABLESPACE <tablespace_name>;
```

### View tablespace and colocation properties

To check the tablespace and colocation properties of a table, use the [\d](../../api/ysqlsh-meta-commands/#d-s-pattern-patterns) meta-command on the table as follows:

```sql
\d table_name;
```

You should see output similar to the following:

```output
                 Table "public.t"
 Column |  Type   | Collation | Nullable | Default
--------+---------+-----------+----------+---------
 col    | integer |           |          |
Tablespace: "us_east_1a_zone_tablespace"
Colocation: true
```

### List all tablegroups and associated tablespaces

To list all tablegroups and their associated tablespaces, use the following query:

```sql
SELECT * FROM pg_yb_tablegroup;
```

You should see output similar to the following:

```output
   grpname       | grpowner | grptablespace | grpacl | grpoptions
-----------------+----------+---------------+--------+------------
default          |       10 |             0 |        |
colocation_16384 |       10 |         16384 |        |
(2 rows)
```

The `grpname` column represent the tablegroup's name and the `grptablespace` column shows the OID of the associated tablespace.

### Geo-partitioned colocated tables with tablespaces

YugabyteDB supports [row-level geo-partitioning](../../explore/multi-region-deployments/row-level-geo-partitioning/), which distributes each row across child tables based on the region specified by respective tablespaces. This capability enhances data access speed and helps meet compliance policies that require specific data locality.

In a colocated database, you can create colocated geo-partitioned tables to benefit from both colocation and geo-partitioning as follows:

```sql
CREATE TABLE <partitioned_table_name> PARTITION BY LIST(region);

CREATE TABLE <partition_table1> PARTITION OF <partitioned_table_name> FOR VALUES IN (<region1>) TABLESPACE <tablespace1>;

CREATE TABLE <partition_table2> PARTITION OF <partitioned_table_name> FOR VALUES IN (<region2>) TABLESPACE <tablespace2>;

CREATE TABLE <partition_table3> PARTITION OF <partitioned_table_name> FOR VALUES IN (<region3>) TABLESPACE <tablespace3>;
```

### Alter tablespaces for colocated relations

Colocated relations (the strategy of storing related data together to optimize performance and query efficiency) cannot be moved independently from one tablespace to another; instead, you can either move all colocated relations or none. The tablespace to which the colocated relations are being moved must not contain any other colocated relations prior to the move.

To move all relations from one tablespace to another, use the following syntax:

```sql
ALTER TABLE ALL IN TABLESPACE tablespace_1 SET TABLESPACE tablespace_2 CASCADE;
```

This command moves all relations in `tablespace_1` to `tablespace_2`, including any non-colocated relations. Note that keyword CASCADE is required to move the colocated relations.

To move only colocated relations, you can specify a table to colocate with using the following syntax:

```sql
ALTER TABLE ALL IN TABLESPACE tablespace_1 COLOCATED WITH table1 SET TABLESPACE tablespace_2 CASCADE;
```

This command moves only the relations present in `tablespace_1` that are colocated with `table1`, where `table1` must be a colocated table.

Move a single non-colocated table using the following syntax:

```sql
ALTER TABLE <table_name> SET TABLESPACE <new_tablespace>;

```

#### Failure scenarios for altering tablespaces

The following failure scenarios are applicable to the alter commands from the [Alter tablespaces for colocated relations](#alter-tablespaces-for-colocated-relations) section.

**Scenario 1**: Moving to a tablespace which already contains colocated relations

Consider the following example:

```sql
CREATE TABLE t1 (col INT PRIMARY KEY, col2 INT) TABLESPACE tsp1;
CREATE TABLE t2 (col INT PRIMARY KEY, col2 INT) TABLESPACE tsp1;
CREATE TABLE t3 (col INT PRIMARY KEY, col2 INT) TABLESPACE tsp2;

ALTER TABLE ALL IN TABLESPACE tsp1 SET TABLESPACE tsp2;
```

```output
ERROR:  cannot move colocated relations to tablespace tsp2, as it contains existing colocated relation
```

```sql
CREATE TABLE t4 (col INT PRIMARY KEY, col2 INT) WITH (COLOCATION = FALSE) TABLESPACE tsp1;

ALTER TABLE t4 SET TABLESPACE tsp2;
```

```output
NOTICE:  Data movement for table t4 is successfully initiated.
DETAIL:  Data movement is a long running asynchronous process and can be monitored by checking the tablet placement in http://<YB-Master-host>:7000/tables
ALTER TABLE
```

Tables `t1` and `t2` are colocated tables belonging to tablespace `tsp1`, while table `t3` is a colocated table in tablespace `tsp2`. Moving colocated tables (for example, `t1` and `t2`) from `tsp1` to `tsp2` is not allowed. However, a non-colocated table (for example, `t4`) from `tsp1` can be moved to `tsp2`.

**Scenario 2**: Moving colocated relations without the keyword CASCADE

If you move colocated relations without the CASCADE keyword, it results in the following error:

```sql
ALTER TABLE ALL IN TABLESPACE tsp1 SET TABLESPACE tsp2;
```

```output
ERROR:  cannot move colocated relations present in tablespace tsp1
HINT:  Use ALTER ... CASCADE to move colcated relations.
```

**Scenario 3**: Using non-colocated tables in COLOCATED WITH syntax

Consider the following example:

```sql
CREATE TABLE t1 (col INT PRIMARY KEY, col2 INT) TABLESPACE tsp1;
CREATE TABLE t2 (col INT PRIMARY KEY, col2 INT) WITH (COLOCATION = FALSE) TABLESPACE tsp1;

ALTER TABLE ALL IN TABLESPACE tsp1 COLOCATED WITH t2 SET TABLESPACE tsp2 CASCADE;
```

```output
ERROR:  the specified relation is non-colocated which can't be moved using this command
```

### Back up and restore colocated database using ysql_dump

You can back up and restore a database with colocated tables and tablespaces in two ways:

- With `--use_tablespaces` option in [ysql_dump](../../admin/ysql-dump/). Using this option during backup and restore includes tablespace information. The restored database requires the target universe to contain the necessary nodes for tablespace creation.

- Without `--use_tablespaces` option. When this option is omitted, tablespace information is not stored during backup and restore, and all relations are restored to the YugabyteDB default tablespace `pg_default`.

  After the restore, all colocated entities remain colocated, and you can create tablespaces post-restore as needed. Tables can then be moved to the desired tablespaces using the ALTER syntax.

  - To move non-colocated tables, use the syntax to [alter the tablespace of a non-colocated table](#create-a-colocated-table-in-a-tablespace).

  - To move colocated relations, use the following variant of the previously mentioned command:

      ```sql
      ALTER TABLE ALL IN TABLESPACE pg_default COLOCATED WITH table1 SET TABLESPACE new_tablespace CASCADE;
      ```

      This command moves all restored colocated relations in the default tablespace `pg_default` that are colocated with `table1` to `new_tablespace`.

  - Consider the following example schema for before and after backup or restore operation:

      ```sql
      CREATE TABLE t1 (a INT, region VARCHAR, c INT, PRIMARY KEY(a, region)) PARTITION BY LIST (region);
      CREATE TABLE t1_1 PARTITION OF t1 FOR VALUES IN ('USWEST') TABLESPACE tsp1;
      CREATE TABLE t1_2 PARTITION OF t1 FOR VALUES IN ('USEAST') TABLESPACE tsp2;
      CREATE TABLE t1_3 PARTITION OF t1 FOR VALUES IN ('APSOUTH') TABLESPACE tsp3;
      CREATE TABLE t1_default PARTITION OF t1 DEFAULT;

      CREATE TABLE t2 (a INT, region VARCHAR, c INT, PRIMARY KEY(a, region)) PARTITION BY LIST (region);
      CREATE TABLE t2_1 PARTITION OF t2 FOR VALUES IN ('USWEST') TABLESPACE tsp1;
      CREATE TABLE t2_2 PARTITION OF t2 FOR VALUES IN ('USEAST') TABLESPACE tsp2;
      CREATE TABLE t2_3 PARTITION OF t2 FOR VALUES IN ('APSOUTH') TABLESPACE tsp3;
      CREATE TABLE t2_default PARTITION OF t2 DEFAULT;
      ```

      The tablegroup information would look like the following:

      ```sql
      \dgrt
      ```

      ```output
                         List of tablegroup tables
          Group Name    | Group Owner |    Name    | Type  |  Owner
      ------------------+-------------+------------+-------+----------
       colocation_16384 | postgres    | t2_1       | table | yugabyte
       colocation_16384 | postgres    | t1_1       | table | yugabyte
       colocation_16385 | postgres    | t2_2       | table | yugabyte
       colocation_16385 | postgres    | t1_2       | table | yugabyte
       colocation_16386 | postgres    | t2_3       | table | yugabyte
       colocation_16386 | postgres    | t1_3       | table | yugabyte
       default          | postgres    | t2_default | table | yugabyte
       default          | postgres    | t1_default | table | yugabyte
       default          | postgres    | t2         | table | yugabyte
       default          | postgres    | t1         | table | yugabyte
      (10 rows)
      ```

      The Group Name column shows which entities are colocated.

       Each tablegroup belongs to a different tablespace, as shown in the grptablespace column in the following table.

      ```sql
      SELECT * FROM pg_yb_tablegroup;
      ```

      ```output
           grpname      | grpowner | grptablespace | grpacl | grpoptions
      ------------------+----------+---------------+--------+------------
       default          |       10 |             0 |        |
       colocation_16384 |       10 |         16384 |        |
       colocation_16385 |       10 |         16385 |        |
       colocation_16386 |       10 |         16386 |        |
      (4 rows)
      ```

      The same information after backup or restore without the `--use_tablespaces` option looks like the following:

      ```sql
      \dgrt
      ```

      ```output
                     List of tablegroup tables
              Group Name        | Group Owner |    Name    | Type  |  Owner
      --------------------------+-------------+------------+-------+----------
       colocation_restore_16393 | postgres    | t2_1       | table | yugabyte
       colocation_restore_16393 | postgres    | t1_1       | table | yugabyte
       colocation_restore_16399 | postgres    | t2_2       | table | yugabyte
       colocation_restore_16399 | postgres    | t1_2       | table | yugabyte
       colocation_restore_16405 | postgres    | t2_3       | table | yugabyte
       colocation_restore_16405 | postgres    | t1_3       | table | yugabyte
       default                  | postgres    | t2_default | table | yugabyte
       default                  | postgres    | t1_default | table | yugabyte
       default                  | postgres    | t2         | table | yugabyte
       default                  | postgres    | t1         | table | yugabyte
      (10 rows)
      ```

      The colocation property is still maintained after the backup or restore. But all the tables now reside in the same tablespace (the default one):

      ```sql
      SELECT * FROM pg_yb_tablegroup;
      ```

      ```output
               grpname          | grpowner | grptablespace | grpacl | grpoptions
      --------------------------+----------+---------------+--------+------------
       default                  |       10 |             0 |        |
       colocation_restore_16393 |       10 |             0 |        |
       colocation_restore_16399 |       10 |             0 |        |
       colocation_restore_16405 |       10 |             0 |        |
      (4 rows)
      ```

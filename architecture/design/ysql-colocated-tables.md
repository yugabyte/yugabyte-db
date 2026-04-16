# Colocated Tables

> **Note:** This is a new feature in Beta mode.

In workloads that do very little IOPS and have a small data set, the bottleneck shifts from
CPU/disk/network to the number of tablets one can host per node. 
Since each table by default requires at least one tablet per node, a YugabyteDB cluster with 5000
relations (tables, indexes) will result in 5000 tablets per node.
There are practical limitations to the number of tablets that YugabyteDB can handle per node since each tablet
adds some CPU, disk and network overhead. If most or all of the tables in YugabyteDB cluster are small tables,
then having separate tablets for each table unnecessarily adds pressure on CPU, network and disk.

To help accomodate such relational tables and workloads, we've added support for colocating SQL tables.
Colocating tables puts all of their data into a single tablet, called the *colocation tablet*.
This can dramatically increase the number of relations (tables, indexes, etc) that can
be supported per node while keeping the number of tablets per node low.
Note that all the data in the colocation tablet is still replicated across 3 nodes (or whatever the replication factor is).

## Motivation

This feature is desirable in a number of scenarios, some of which are described below.

### 1. Small datasets needing HA or geo-distribution
Applications that have a smaller dataset may fall into the following pattern:
* They require large number of tables, indexes and other relations created in a single database.
* The size of the entire dataset is small. Typically, this entire database is less than 500GB in size.
* Need high availability and/or geographic data distribution.
* Scaling the dataset or the number of IOPS is not an immediate concern.

In this scenario, it is undesirable to have the small dataset spread across multiple nodes because this might affect performance of certain queries due to more network hops (for example, joins).

**Example:** a user identity service for a global application. The user dataset size may not be too large, but is accessed in a relational manner, requires high availability and might need to be geo-distributed for low latency access.

### 2. Large datasets - a few large tables with many small tables
Applications that have a large dataset may fall into the pattern where:
* They need a large number of tables and indexes.
* A handful of tables are expected to grow large, needing to be scaled out.
* The rest of the tables will continue to remain small.

In this scenario, only the few large tables would need to be sharded and scaled out. All other tables would benefit from colocation, because queries involving all tables except the larger ones would not need network hops.

**Example:** An IoT use case, where one table records the data from the IoT devices while there are a number of other tables that store data pertaining to user identity, device profiles, privacy, etc.

### 3. Scaling the number of databases, each database with a small dataset
There may be scenarios where the number of databases grows rapidly, while the dataset of each database is small. This is characteristic of a microservices-oriented architecture, where each microservice needs its own database. These microservices are hosted in dev, test, staging, production and other environments. The net result is a lot of small databases, and the need to be able to scale the number of databases hosted. Colocated tables allow for the entire dataset in each database to be hosted in one tablet, enabling scalability of the number of databases in a cluster by simply adding more nodes.

**Example:** Multi-tenant SaaS services where one database is created per customer. As new customers are rapidly on-boarded, it becomes necessary to add more databases quickly while maintaining high-availability and fault-tolerance of each database.

## Tradeoffs

Fundamentally, colocated tables have the following tradeoffs:
* **Higher performance - no network reads for joins**. All of the data across the various colocated tables is local, which means joins no longer have to read data over the network. This improves the speed of joins.
* **Support higher number of tables - using fewer tablets**. Because multiple tables and indexes can share one underlying tablet, a much higher number of tables can be supported using colocated tables.
* **Lower scalability - until removal from colocation tablet**. The assumptions behind tables that are colocated is that their data need not be automatically sharded and distributed across nodes. If it is known apriori that a table will get large, it can be opted out of the colocation tablet at creation time. If a table already present in the colocation tablet gets too large, it can dynamically be removed from the colocation tablet to enable splitting it into multiple tablets, allowing it to scale across nodes.


## Usage

This section describes the intended usage of this feature. There are three aspects to using this feature:

### 1. Enable colocation of tables in a database by default

When creating a database, you can specify that every table created in the database should be colocated by default into one tablet. This can be achieved by setting the `colocated` property of the database to `true`, as shown below.

__Syntax:__

```sql
CREATE DATABASE name WITH colocated = <true|false>
```

With `colocated = true`, create a colocation tablet whenever a new YSQL database is created.
The default is `colocated = false`.
We'll provide a gflag `--ysql_colocation`, which, if enabled, will set the default to `colocated = true`.

### 2. Create tables opted out of colocation

In some databases, there may be many small tables and a few large tables. In this case, the database should be created with colocation enabled as shown above so that the small tables can be colocated in a single tablet. The large tables opt out of colocation by overriding the `colocated` property at the table level to `false`.

__Syntax:__

```sql
CREATE TABLE name (columns) WITH (colocated = <true|false>)
```

With `colocated = false`, create separate tablets for the table instead of creating the table in the colocation tablet.
The default here is `colocated = true`.

> **Note:** This property should be only used when the parent DB is colocated. It has no effect otherwise.

### 3. Create indexes opted out of colocation

The only use for this option is the ability to have a non-colocated index on a colocated table.

__Syntax:__

```sql
CREATE INDEX ON name (columns) WITH (colocated = <true|false>)
```

The behavior of this option is a bit confusing, so it is outlined below.

| | `CREATE TABLE ... WITH (colocated = true)` | `CREATE TABLE ... WITH (colocated = false)` |
| --- | --- | --- |
| `CREATE INDEX ... WITH (colocated = true)` | colocated table; colocated index | non-colocated table; non-colocated index |
| `CREATE INDEX ... WITH (colocated = false)` | colocated table; non-colocated index | non-colocated table; non-colocated index |

Observe that it is not possible to have a colocated index on a non-colocated table.
The default here is `colocated = true`.

> **Note:** This property should be only used when the parent DB is colocated. It has no effect otherwise.

### 4. Specify colocation at schema level

In some situations, it may be useful for applications to create multiple schemas (instead of multiple DBs) and use 1 tablet per schema. Using this configuration has the following advantages:

* Enables applications to use PG connection pooling. Typically, connection pools are created per database.
  So, if applications have a large number of databases, they cannot use connection pooling effectively.
  Connection pools become important for scaling applications.
* Reduces system catalog overhead on the YB-Master service. Creating multiple databases adds more overhead since postgres internally creates 200+ system tables per database.

The syntax for achieving this is shown below.

__Syntax:__

```sql
CREATE SCHEMA name WITH colocated = <true|false>
```

## Design

As per the current design, the colocation tablet will not split automatically. However, one or more of these colocated tables can be pulled out of the colocation tablet and allowed to split (pre-split, manually split or automatically split) to enable them to scale out across nodes.

### Single vs Multiple RocksDB

Today, there is one RocksDB created per tablet. This RocksDB only has data for a single tablet. With multiple tables in a single tablet, we have two options:

1. Use single RocksDB for the entire tablet (i.e. for all tables).
1. Use multiple RocksDBs with one RocksDB per table.

We decided to use single RocksDB for entire tablet. This is because:

* It enables us to leverage code that was written for postgres system tables. Today, all postgres system tables are colocated on a single tablet in master and uses a single RocksDB. We can leverage a lot of that code.
* We may hit other scaling limits with multiple RocksDBs. For example, it's possible that having 1 million RocksDBs (1000 DBs, with 1000 tables per DB) will cause other side effects.

### Create, Drop, Truncate

#### Create Database

When a database is created with `colocated=true`, catalog manager will need to create a tablet for this database. Catalog manager's `NamespaceInfo` and `TableInfo` objects will need to maintain a colocated property.

Today, tablet's `RaftGroupReplicaSuperBlockPB` has a `primary_table_id`. For system tables, this is the table ID of sys catalog table. Primary table ID seems to be used in two ways:

1. Rows of primary table ID are not prefixed with table ID while writing to RocksDB. All other table rows are prefixed with cotable ID.
1. Remote bootstrap client checks that tablet being bootstrapped has a primary table. (It's not clear why it needs this.)

Since there is no "primary table" in a colocated DB, we have two options:

1. Make this field optional. We'll need to check some dependencies like remote bootstrap to see if this is possible.
1. Create a parent table for the database, and make that the primary table.

Tablet creation requires a schema and partition range to be specified. In this case, schema will empty and partition range will be _[-infinity, infinity)_.

Currently, RocksDB files are created in the directory `tserver/data/rocksdb/table-<id>/tablet-<id>/`. Since this tablet will have multiple tables, the directory structure will change to `tserver/data/rocksdb/tablet-<id>/`.

#### Create Table

When a table is created in a colocated database, catalog manager should add that table to the tablet that was created for the database and not create new tablets.
It'll need to invoke `ChangeMetadataRequest` to replicate the table addition.

If the table is created with `colocated=false`, then it should go through the current table creation process and create tablets for the table.

#### Drop Table

When a colocated table is dropped, catalog manager should simply mark the table as deleted (and not remove any tablets). It'll then need to invoke a `ChangeMetadataRequest` to replicate the table removal. Note that, currently, `ChangeMetadata` operation does not support table removal, and we'll need to add this capability.

To delete the data, a table-level tombstone can be created.
Special handling needs to be done for this tombstone in areas like compactions and iterators.

If the table being dropped has `colocated=false`, then it should go through the current drop table process and delete the tablets.

#### Drop Database

This should delete the database from sys catalog and also remove the tablets created.

#### Truncate Table

Like `DROP TABLE`, a table-level tombstone should be created.
However, catalog manager should not mark the table as deleted.

#### Postgres Metadata

It'll be useful to store colocated property in postgres system tables (`pg_database` for database and `pg_class` for table) for two reasons:

1. YSQL dump and restore can use to generate the same YB schema.
1. Postgres cost optimizer can use it during query planning and optimization.

We can reuse `tablespace` field of these tables for storing this information. This field in vanilla postgres dictates the directory / on disk location of the table / database. In YB, we can repurpose it to indicate tablet location.

### Client Changes

* Add `is_colocated` in `SysTabletEntryPB` to indicate if a tablet is a colocated tablet.
* Add `is_colocated` in `CreateTabletRequestPB`.
* For `SysCatalogTable`, `Tablet::AddTable` is called when creating a new table. There is no corresponding way to do that when the tablet is in a tserver. Hence we need to add an RPC `AddTableToTablet` in the `TabletServerAdminService`, and add `AsyncAddTableToTablet` task to call that RPC.
* Modify `RaftGroupMetadata::CreateNew` to take `is_colocated` parameter. If the table is colocated, use `data/rocksdb/tablet-<id>` as the `rocksdb_dir` and `wal/tablet-<id>` as the `wal_dir`.

### Load Balancing

Today, load balancing looks at all tables and then balances all tablets for each table. We need to make the load balancer aware of tablet colocation in order to avoid balancing the same tablet.

### Local and Remote Bootstrap

This does not require any changes.

### Backup and Restore

Since backup and restore is done at the tablet level, for colocated tables, we cannot backup individual tables.
We'll need to make the backup / restore scripts work for the entire DB instead of per table.

### Postgres system tables bloat

Having a huge number of databases can result in high load on the master since each database will create 200+ postgres system tables.
We need to test the limit on the number of databases that we can create without impacting master and cluster performance.

### Master / Tserver UI

No impact on master UI since all views are per table or per tserver.

Tserver UI tables view uses tablet peers to get the table information. Today, it'll only display data for the primary table. We'll need to change this to show all tables in colocated tablet.
Additionally, the tables view shows the on disk size for every table. This per table size is going to be inaccurate for colocated tablets. We'll need to change this view to reflect data for colocated tablets accurately.

### Metrics

TODO

### Pulling out tables from a colocated tablet

When a table grows large, it'll be useful to have the ability to pull the table out of its colocated tablet in order to scale. We won't provide an automated way to do this in 2.1. This can be done manually using the following steps:

1. Create a table with the same schema as the table to be pulled out.
1. Dump contents of original table using `ysql_dump` or `COPY` command and importing that into the new table.
1. Drop original table.
1. Rename new table to the same name as the original table.

### CDC / 2DC

Today, CDC and 2DC create change capture streams per table.
Each stream will cause CDC producers and CDC consumers to start up for each tablet of the stream.
With colocated tables, we need to provide an ability to create a CDC stream per database.
We'll also need an ability to filter out rows for tables that the user is not interested in consuming.
Similarly, generating producer-consumer maps is done per table today. That will need to change to account for colocated tables.

### Yugax Platform

Today, YW provides the ability to backup tables. This will need to change since we cannot backup individual tables anymore.
We need to provide a back up option for a DB. However, this also depends on supporting backups for YSQL tables.

### Dynamic Tablet Splitting

Current design for tablet splitting won't work as is for colocated tablets.
The design finds a split key (approximate mid-key) for the tablet and splits the tablet range into two partitions.
Since colocated tablets have multiple tables with different schemas, we cannot find the "mid point" of the tablet.
We could potentially split the tablet such that some tables are in one tablet and other tables are in the second tablet, but this will require some changes to the design.


[![Analytics](https://yugabyte.appspot.com/UA-104956980-4/architecture/design/ysql-colocated-tables.md?pixel&useReferer)](https://github.com/yugabyte/ga-beacon)

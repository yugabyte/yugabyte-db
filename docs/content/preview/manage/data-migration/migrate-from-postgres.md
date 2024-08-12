---
title: Migrate from Postgres
headerTitle: Migrate from Postgres
linkTitle: Migrate from Postgres
description: Guide to migration from PostgreSQL
menu:
  preview:
    identifier: migrate-from-postgres
    parent: manage-bulk-import-export
    weight: 710
type: docs
---

As businesses grow and demand more from their data infrastructure, traditional monolithic databases like PostgreSQL can sometimes struggle to keep up with the scalability, availability, and performance requirements of modern, cloud-native applications. To address these challenges, many organizations are turning to distributed databases like YugabyteDB, which offer horizontal scalability, global distribution, and built-in fault tolerance.

This guide is designed to help you smoothly transition your data and applications from a monolithic PostgreSQL database to YugabyteDB’s distributed architecture. Whether you’re looking to scale out your existing applications, reduce latency for globally dispersed users, or ensure continuous availability, migrating to YugabyteDB can provide the foundation you need for a robust, future-proof database infrastructure.

This guide will walk you through the essential steps and best practices for migrating your data, including planning the migration, setting up the YugabyteDB cluster, transforming your schema, migrating your data, and optimizing your applications for a distributed environment. By following these steps, you can minimize downtime, preserve data integrity, and leverage YugabyteDB’s advanced features to meet your evolving business needs.

{{<note>}}
The preferred way of migration of a PostgreSQL database on to YugabyteDB is via [YB Voyager](../../../yugabyte-voyager/migrate/). Voyager has been designed to handle various corner cases correctly to minimize errors and achieve faster migration. This guide provides general guidelines to be considered during migration.
{{</note>}}

## PostgreSQL Compatibility

### API compatibility

While YugabyteDB is a distributed database, it leverages PostgreSQL's query layer to provide the YSQL API that is wire-compatible and syntax-compatible with PostgreSQL.

![Monolithic vs Distributed SQL](/images/architecture/monolithic-vs-distributed.png)

### Feature support

Implementing all PostgreSQL features in a distributed system can be challenging, and some features are still under development. To ensure that the features your applications depend on are supported in the version of YugabyteDB you are using, it is important to double-check that the feature is included in the list of supported features.

{{<lead link="../../../explore/ysql-language-features/postgresql-compatibility/#unsupported-postgresql-features">}}
Please review the [list of unsupported features](../../../explore/ysql-language-features/postgresql-compatibility/#unsupported-postgresql-features) to make sure that the PostgreSQL features used by your application are supported by YugabyteDB.
{{</lead>}}

### Extension support

PostgreSQL offers extensions that greatly enhance its capabilities, allowing users to tailor and extend its functionality according to their needs. YugabyteDB includes many of these extensions. It is important to double-check that the extensions used by your application are supported by YugabyteDB.

{{<lead link="../../../explore/ysql-language-features/pg-extensions#supported-extensions">}}
To verify that the extensions your applications use are available in YugabyteDB, please check the [list of supported extensions](../../../explore/ysql-language-features/pg-extensions#supported-extensions).
{{</lead>}}

## Data distribution schemes

Postgres is a monolithic database that holds your data on just one machine while YugabyteDB is a distributed SQL database that distributes table rows across nodes in the cluster using two [data distribution](../../../explore/going-beyond-sql/data-sharding/) methods: [Hash](../../../explore/going-beyond-sql/data-sharding/#hash-sharding) sharding and [Range](../../../explore/going-beyond-sql/data-sharding/#range-sharding) sharding. This is a very key difference in modeling your data as you have to understand how your data is distributed to write optimal queries to store and retrieve data.

In Hash sharding, rows are distributed based on a hash of the primary key and stored ordered on the hash of the primary key. This leads to a very good distribution of data but no useful ordering. This is great for point lookups like,

```sql
-- point lookup query
select * from users where id = 6;
```

For eg., for a census table that is hash-sharded on the `id` of the person, the distribution could look something like,

![Hash sharded census](/images/develop/data-modeling/hash-sharded-census.png)

In Range sharding, the distribution is based on the actual value of the primary key. For example, if the census table is range-sharded on the `id`, the data distribution could look like,

![Range sharded census](/images/develop/data-modeling/range-sharded-census.png)

As the data is stored ordered in the natural sort order of the primary key, contiguous keys like 1,2,3,4 are stored together. This enables range queries to be very efficient. Eg.,

```sql
-- range query
select * from users where id>2 and id<6;
```

For those migrating from PostgreSQL, Range sharding may be a more natural fit, as PostgreSQL also stores data in the order of the primary keys. Range sharding is particularly beneficial if your applications rely on a combination of range and point lookup queries.

## Data modeling

Data modeling for distributed SQL databases differs from monolithic PostgreSQL due to the unique challenges and opportunities presented by a distributed architecture. While many principles of relational database design remain consistent, distributed databases require additional considerations to ensure performance, scalability, and consistency across multiple nodes.

### Primary key

It is of paramount importance to choose the Primary key of the table wisely as the [distribution of table data](#data-distribution-schemes) across the various nodes in the system is dependent on the Primary Key.

{{<lead link="../../../develop/data-modeling/primary-keys-ysql/">}}
Please refer to [Designing optimal primary keys](../../../develop/data-modeling/primary-keys-ysql/) for details on how to design primary keys for your tables.
{{</lead>}}

### Secondary indexes

For improved performance of alternate query patterns that dont involve the primary key, you might have to create secondary indexes. For eg., if the primary key of your table is based on the `id` of the user , but you have query lookup based on the name of the user, you would have to create an index on the `name` column for optimal performance of your name based queries.

Just like tables, in YugabyteDB, indexes are global and are implemented just like tables. They are split into tablets and distributed across the different nodes in the cluster. The distribution of indexes is based on the primary key of the index and is independent of how the main table is sharded and distributed. It is important to note that indexes are not colocated with the base table.

{{<lead link="../../../develop/data-modeling/secondary-indexes-ysql/">}}
Please refer to [Designing secondary indexes](../../../develop/data-modeling/secondary-indexes-ysql/) for details on how to design secondary indexes to speed up alternate query patterns.
{{</lead>}}

### Hot shards

The "hot shard" problem in distributed databases refers to a situation where a particular shard in the database receives a disproportionate amount of traffic compared to other shards leading to increased latency and performace degradation.

The hot shard problem arises when there is a skewed data distribution or when an specific value or data pattern becomes popular. This can be easily addressed by choosing a different ordering of the primary key columns or distributing your data based on more columns.

{{<lead link="../../../develop/data-modeling/hot-shards-ysql/">}}
To understand different ways to address the hot shard issue, see [Avoiding hotspots](../../../develop/data-modeling/hot-shards-ysql/)
{{</lead>}}

### Colocated tables

By default all the rows of the table are distributed across multiple tablets and spread across the different nodes in the cluster. This is very beneficial for large tables. But for smaller tables like lookup or reference it would be advisable to keep all the rows together in a single tablet for performance reasons. For this purpose, YugabyteDB supports the concept of Colocated tables where all the rows of the table reside just in one tablet and hence on just one node.

It is advisable to choose colocation for tables with small tables that dont grow much for eg., less than 1 million rows, or smaller than 1 GB.

{{<lead link="../../../architecture/docdb-sharding/colocated-tables/">}}
For more information on when and how to create colocated tables, see [Colocated tables](../../../architecture/docdb-sharding/colocated-tables/")
{{</lead>}}

## Schema Migration

Once you have a detailed understanding of data distribution works in YugabyteDB and how it affects your data model you can export the schema and make appropriate changes. Here are some important things to consider when migrating your schema to YugabyteDB.

### Export schema

You can either use the [ysql_dump](../../../admin/ysql-dump/) utility to export a YugabyteDB-friendly version of the schema and, therefore, includes some of the schema modifications like

```bash
ysql_dump --schema-only -h source_host -U source_user source_db > schema.sql
```

or use [yb-voyager export schema](../../../yugabyte-voyager/reference/schema-migration/export-schema/) command.

### Changes to schema

Many changes might be required depending on the use case. You can also use [yb-voyager analyze schema](../../../yugabyte-voyager/reference/schema-migration/analyze-schema/) command to analyze the schema and get suggestions for modifications. Some possible changes are listed below.

### Specify `PRIMARY KEY` inline

When creating the table, decide on the correct primary key and specify it inline as part of the CREATE TABLE definition. This is because the distribution of data is dependent on the primary key. If the primary key is defined or altered after the data has been loaded, it would result in a table re-write, which could be an expensive operation depending on the size of the table.

### Choose the correct sort order

When defining the primary key for the table and indexes make sure you pick the sort order (`ASC`/`DESC`) debased on your common queries as this would enhance the performance of `ORDER BY` clause by avoiding an explicit sort operation.

### Colocation

In many scenarios, there may be a large number of database objects (tables and indexes specifically) which hold a relatively small dataset. In such cases, creating a separate tablet for each table and index could drastically reduce performance. Colocating these tables and indexes into a single tablet can drastically improve performance.

Enabling the colocation property at a database level causes all tables created in this database to be colocated by default. Tables in this database that hold a large dataset or those that are expected to grow in size over time can be opted out of the colocation group, which would cause them to be split into multiple tablets.

### Pre-split large tables

For larger tables and indexes that are hash-sharded, specify the number of initial tablet splits desired as part of the DDL statement of the table. This can be very beneficial to distribute the data of the table across multiple nodes right from the start.

For larger tables and indexes that are range-sharded and the value ranges of the primary key columns are known ahead of time, pre-split them at the time of creation. This is especially beneficial for range sharded tables/indexes.

{{<lead link="../../../architecture/docdb-sharding/tablet-splitting/">}}
To understand how to pre-split tables, see [Tablet splitting](../../../architecture/docdb-sharding/tablet-splitting/)
{{</lead>}}

### Collation limitations

Be aware of the caveats of collation support in YugabyteDB and choose the right collation for your database and tables. For example, not all collations are supported and databases can only be created with "C" collation.

{{<lead link="../../../explore/ysql-language-features/advanced-features/collations/#ysql-collation-limitations">}}
Refer to [Collations](../../../explore/ysql-language-features/advanced-features/collations/#ysql-collation-limitations) for more details on the limitations related to collations.
{{</lead>}}

### Cache your sequences

Sequences are created with a default cache size of 100. Depending on your application usage, it might be worth to choose a larger cache size. YugabyteDB now supports server size caching of sequences with [ysql_sequence_cache_method](../../../reference/configuration/yb-tserver/#ysql-sequence-cache-method) gflag when set to the value `server`. This is suggested value for most applications. Given that multiple connections will use the same cache, provision a much bigger sequence(~10X) cache when opting for server-side caching.

You can set the cache size either during [CREATE SEQUENCE](../../../api/ysql/the-sql-language/statements/ddl_create_sequence/#cache-int-literal) or using [ALTER SEQUENCE](../../../api/ysql/the-sql-language/statements/ddl_alter_sequence/#cache-int-literal)

### Opt for UUIDs over sequences

UUIDs are globally unique identifiers that can be generated on any node without requiring any global inter-node coordination. Although the performance of sequences have significantly improved with server side caching, there are scenarios like multi-region deployment where the use of UUIDs would be a better choice for performace.

{{<lead link="../../../develop/data-modeling/primary-keys-ysql/#automatically-generating-the-primary-key">}}
To understand the differences between UUID, serial and sequences, see [Automatically generating the primary key](../../../develop/data-modeling/primary-keys-ysql/#automatically-generating-the-primary-key)
{{</lead>}}

### Importing schema

Once you have completed your schema changes, you can either use the ysqlsh command to import the modified schema onto YugabyteDB as,

```bash
ysqlsh -h yugabyte_host -U yugabyte_user -d yugabyte_db -f schema.sql
```

or use YB Voyager to import the schema using [yb-voyage import schema](../../../yugabyte-voyager/reference/schema-migration/import-schema/) command.

## Data Migration

The data from the source PostgreSQL database can be exported either using the [COPY command into CSV files](../bulk-export-ysql) or exported to flat files using `ysql_dump` as:

```bash
ysql_dump --data-only --disable-triggers -h source_host -U source_user source_db > data.sql
```

or using the [yb-voyager export data](../../../yugabyte-voyager/reference/data-migration/export-data/) command (recommended).


The exported data can be imported into YugabyteDB either using [COPY FROM](../../../manage/data-migration/bulk-import-ysql/#import-data-from-csv-files) command or using `ysqlsh` as,

```bash
ysqlsh -h yugabyte_host -U yugabyte_user -d yugabyte_db -f data.sql
```

or using the recommended [yb-voyager import data](../../../yugabyte-voyager/reference/data-migration/import-data/) command.

### What to migrate

Depending on your business needs you can choose which and how much data you want to migrate at a given point in time.

**Big Bang Migration**: Migrate all data and applications in one go. This approach is suitable for smaller databases or when a downtime window is acceptable.

**Phased Migration**: Migrate the data in phases, with part of the application pointing to the old database and part to the new one. This is suitable for larger databases with low downtime tolerance.

**Hybrid Approach**: Combine the above strategies, migrating non-critical data first, followed by critical data during a planned downtime window.

Immaterial of how much data you decide to migrate, you can choose from the following strategies to actually execute the migration.

### How to migrate

**Offline Migration**: You can take down the system and import the exported data. This approach is typically used when downtime is acceptable or the system is not required to be available during the migration.

{{<lead link="../../../yugabyte-voyager/migrate/migrate-steps/">}}
For more details, see [Offline migration](../../../yugabyte-voyager/migrate/migrate-steps/)
{{</lead>}}

**Live Migration**: Live migration aims to minimize downtime by keeping the application running during the migration process. Data is copied from the source database to the target database while the application is still live, and a final switchover is made once the migration is complete.

{{<lead link="../../../yugabyte-voyager/migrate/live-migrate/">}}
For more details, see [Live migration](../../../yugabyte-voyager/migrate/live-migrate/)
{{</lead>}}

**Live Migration with Fall-Forward**: Live migration with fall-forward is a variant of live migration where, once the application has switched to the new database, there is no option to revert to the old database. This strategy is typically used when the new database is considered stable and there is confidence in the migration process.

{{<lead link="../../../yugabyte-voyager/migrate/live-fall-forward/">}}
For more details, see [Live migration with fall-forward](../../../yugabyte-voyager/migrate/live-fall-forward/)
{{</lead>}}

**Live Migration with Fall-Back**: Live migration with fall-back provides a safety net by allowing a return to the original database if issues are encountered after the cutover to the new database. This strategy involves maintaining bidirectional synchronization between the source and target databases for a period after the migration.

{{<lead link="">}}
For more details, see [Live migration with fall-back](../../../yugabyte-voyager/migrate/live-fall-back/)
{{</lead>}}

## Verification

Once the migration is complete you need to take steps to verify the migration. You can do this in few ways.

- **Functional testing**: Verify that all application queries work as expected in YugabyteDB.
- **Consistency checks**: You should run data consistency checks between PostgreSQL and YugabyteDB to ensure no data is lost or corrupted during the migration by [verifying the database objects](../bulk-import-ysql#verify-database-objects) and by [verifying row counts](../bulk-import-ysql#verify-row-counts-for-tables).

## Application migration

When porting an existing PostgreSQL application to YugabyteDB you can follow a set of best practices to get the best out of your new deployment.

{{<lead link="../../../develop/best-practices-ysql/">}}
For a full list of tips and tricks for high performance and availability, see [Best practices](../../../develop/best-practices-ysql/)
{{</lead>}}

### Retry transactions on conflicts

YugabyteDB uses the error code 40001 (serialization_failure) for retryable transaction conflict errors. We recommend retrying the transactions from the application upon encountering these errors.

{{<lead link="../../../develop/learn/transactions/transactions-retries-ysql/#client-side-retry">}}
For application-side retry logic, see [Client-side retry](../../../develop/learn/transactions/transactions-retries-ysql/#client-side-retry)
{{</lead>}}

### Distribute load evenly across the cluster

All nodes (YB-TServers) in the cluster are identical and are capable of handling queries. However, the client drivers of PostgreSQL are designed to communicate only with a single endpoint (node). In order to utilize all the nodes of the cluster evenly, the queries from the application would need to be distributed uniformly across all nodes of the cluster. There are two ways to accomplish this:

- **Load balancer**: Use a load balancer to front all the nodes of the cluster. The load balancer should be set to round-robin all requests across the nodes in the cluster.

- **Smart driver**: YugabyteDB ships a [smart driver](../../../drivers-orms/smart-drivers/) in multiple languages that can automatically distribute connections to the various nodes in the cluster with minimum configuration.

### Increasing throughput

There are many applications where handling a large number of client connections is critical. There are few strategies to accomplish this:

- **Connection pool:** Use a connection pool in your application such as the Hikari pool. Using a connection pool drastically reduces the number of connections by multiplexing a large number of logical client connections onto a smaller number of physical connections across the nodes of the YugabyteDB cluster.

- **Increase number of nodes in cluster:**  Note that the number of connections to a YugabyteDB cluster scales linearly with the number of nodes in the cluster. By deploying more nodes with smaller vCPUs per node, it may be possible to get more connections. As an example, a 10 node cluster consisting of 32 vCPU per node can handle 3000 connections. If more connections are desirable, deploying a 20 node cluster with 16 vCPUs per node (which is equivalent to the 10 node, 32 vCPU cluster) can handle 6000 connections.

- **Connection Manager** YugabyteDB now ships with an inbuild connection pooler called [Connection manager](../../../explore/going-beyond-sql/connection-mgr-ysql/). This is built on top of the oss Odyssey project. The connection manager works by multiplexing many client connection over few physical connections to the database.

## Post-Migration Activities

After the migration is complete, several key steps must be taken to ensure the system operates smoothly and without issues. These include verifying the integrity of migrated data, testing system performance, addressing any compatibility concerns, and monitoring the system closely for any unexpected behavior. Properly addressing these aspects will help ensure a seamless transition and the reliable functioning of the system.

### Monitoring and Optimization

Regularly monitor the target database to ensure it is performing efficiently. This includes tracking metrics such as query execution times, CPU usage, memory consumption, and disk I/O. Pay close attention to any errors or warnings that arise, as they can indicate potential issues with the database configuration, queries, or underlying infrastructure.

After the migration, review and optimize queries to ensure they are well-suited for the new database engine. Different database systems may handle query execution differently, so it's important to adjust queries for optimal performance. Evaluate and refine indexes to match the query patterns of the new database environment. Indexes that were effective in the previous system may need adjustments or reconfiguration to achieve the best performance in the new system.

### Backup and Disaster Recovery

Establish a comprehensive backup strategy that includes full, incremental, and differential backups, depending on your system’s needs. This ensures that you have multiple restore points in case of data loss or corruption.
You should schedule backups to run at regular intervals, ideally during off-peak hours, to minimize the impact on system performance. The frequency of backups should be based on how often the data changes and the criticality of the information.

### Decommissioning

Before proceeding with decommissioning, thoroughly verify the stability and reliability of the target database. Ensure that it is functioning as expected, with no critical errors, performance issues, or compatibility problems. Once confident in the stability of the target database and after securing necessary backups, proceed with decommissioning the source database. This involves shutting down the source system and ensuring that it is no longer accessible to users or applications.

## Learn more

- [Migration by example using Voyager](https://www.yugabyte.com/blog/postgresql-migration-options-using-yugabytedb-voyager/)
- [Understanding Behavior Differences : PostgreSQL vs YugabyteDB](https://www.yugabyte.com/blog/postgresql-vs-distributed-sql-behavior-differences/)
---
title: Colocated tables
headerTitle: Colocated tables
linkTitle: Colocated tables
description: Learn how colocated tables aggregate data into a single tablet.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
aliases:
  - /preview/architecture/docdb/colocated_tables/
menu:
  preview:
    identifier: docdb-colocated-tables
    parent: architecture-docdb-sharding
    weight: 1144
type: docs
---

In workloads that need lower throughput and have a small data set, the bottleneck shifts from CPU, disk, and network to the number of tablets that should be hosted per node. Because each table by default requires at least one tablet per node, a YugabyteDB cluster with 5000 relations (which includes tables and indexes) results in 5000 tablets per node. There are practical limitations to the number of tablets that YugabyteDB can handle per node because each tablet adds some CPU, disk, and network overhead. If most or all of the tables in a YugabyteDB cluster are small tables, then having separate tablets for each table unnecessarily adds pressure on CPU, network, and disk.

To accommodate such relational tables and workloads, YugabyteDB supports [colocating SQL tables](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/ysql-colocated-tables.md). Colocating tables puts all of their data into a single tablet called the colocation tablet. This can dramatically increase the number of relations (tables, indexes, and so on) that can be supported per node while keeping the number of tablets per node low. Note that all the data in the colocation tablet is still replicated across three nodes (or whatever the replication factor is). Large tablets can be dynamically split at a future date if there is need to serve more throughput over a larger data set.

{{< note title="Note" >}}

Colocated tables are not currently recommended for production, as backup-restore is not yet fully supported for colocated tables. Refer to issues [7378](https://github.com/yugabyte/yugabyte-db/issues/7378) and [10100](https://github.com/yugabyte/yugabyte-db/issues/10100) for details.
{{< /note>}}

## Motivation

The use of colocated tables can be beneficial in some cases.

### Small data sets requiring high availability or geo-distribution

Applications that have a smaller data set may fall into the following pattern:

- Require large number of tables, indexes, and other relations created in a single database.
- The size of the entire data set is small. Typically, the entire database is less than 500GB in size.
- Require either high availability or geographic data distribution or both.
- Scaling the data set or the number of input-output operations per second is not an immediate concern.

In this scenario, it is undesirable to have the small data set spread across multiple nodes because
this might affect performance of certain queries due to more network hops (for example, joins).

One of the examples is a user identity service for a global application. The user data set size may not be very large, but is accessed in a relational manner, requires high availability, and might need to be
geo-distributed for low latency access.

### Large data sets with several large tables and many small tables

Applications that have a large data set may fall into the following pattern:

- Require a large number of tables and indexes.
- A small number of tables are expected to grow large, needing to be scaled out.
- The rest of the tables continue to remain small.

In this scenario, only the few large tables would need to be sharded and scaled out. All other tables would benefit from colocation because queries involving all tables, except the larger ones, would not need network hops.

One of the examples is an IoT use case, where one table records the data from the IoT devices, while there are a number of other tables that store data pertaining to user identity, device profiles, privacy, and so on.

### Scaling the number of databases, each database with a small data set

There may be scenarios where the number of databases grows rapidly, while the data set of each database is small.

This is characteristic of a microservices-oriented architecture, where each microservice needs its own database. These microservices are hosted in development, testing, staging, production, and other environments. The result is a lot of small databases, and the need to be able to scale the number of databases hosted. Colocated tables allow for the entire dataset in each database to be hosted in one tablet, enabling scalability of the number of databases in a cluster by simply adding more nodes.

One of the examples is multi-tenant SaaS services where one database is created per customer. As new customers are rapidly on-boarded, it becomes necessary to add more databases quickly while maintaining high availability and fault tolerance of each database.

## Benefits and tradeoffs

Colocated tables have the following benefits:

- Better performance due to no network reads for joins.

  All of the data across the various colocated tables is local, which means joins no longer have to
read data over the network. This improves the speed of joins.

- Possibility of greater number of tables due to using fewer tablets.

  Because multiple tables and indexes can share one underlying tablet, a much greater number of tables can be supported using colocated tables.

The tradeoff is a lower scalability for large tables, until the tables are removed from the colocation tablet.

The assumptions behind tables that are colocated is that their data need not be automatically sharded and distributed across nodes. If it is known a priori that a table will become large, the table can be opted out of the colocation tablet when it is created. If a table already present in the colocation tablet becomes too large, it can be dynamically removed from the colocation tablet to enable splitting it into multiple tablets, allowing it to scale across nodes.

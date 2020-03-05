---
title: Best practices
linkTitle: Best practices
description: Best practices when using YugabyteDB
aliases:
  - /latest/quick-start/best-practices-ysql/
isTocNested: 4
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="{{< ref "best-practices.md" >}}" class="nav-link">
      <i class="icon-" aria-hidden="true"></i>
      General
    </a>
  </li>
  <li >
    <a href="{{< ref "best-practices-ycql.md" >}}" class="nav-link"</a>
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
  <li >
    <a href="" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
</ul>


## Hash vs range primary keys
Choosing the type of partitioning in primary-keys and indexes can be hard. 
There are differences how writes and reads are spread in a distributed database.
Here are things we have to keep in mind when designing schemas:

1. How new data is coming in the app to prevent write/read hotspots
2. Do we need global range querying ?
3. Queries that our application need to answer in the hot path

Range primary keys:

- Allow efficient global range sorting
- Need to be careful when the first column is (bigserial,timestamp) since all writes will go to 1 tablet

Hash primary keys:

- Distribute write queries linearly
- Have slower inefficient global range scanning 


## Co-location
Co-location is a [new feature in beta](../explore/colocated-tables/linux.md) in development where you can put tables of 
a database inside 1 tablet. This increases performance and lowers latency on write transactions, joined queries and aggregations
since they happen in 1 tablet/node.

## Co-partitioning
Co-partitioning tables makes writing/joining transactions across multiple tables that specify
the same partition keys to be faster because the data from tables will reside
in the same tserver, thus removing network that are needed for distributed transactions.

This feature is in our roadmap. You can follow this 
[issue](https://github.com/yugabyte/yugabyte-db/issues/79). 


## Cluster aware drivers
We are working hard to create cluster-aware clients in most popular languages. 
Currently the drivers available are:

1. JDBC driver https://github.com/yugabyte/jdbc-yugabytedb
2. More drivers are coming

In the meantime you can use [proxies and load balancers](#ysql-proxy--load-balancer) which work with all drivers.

## YSQL proxy & load balancer 
Currently YugabyteDB uses the same drivers as Postgresql. Usually they are implemented on top of the
[libpq](https://www.postgresql.org/docs/current/libpq.html) c client. 
Postgresql drivers aren't created with assumptions of a 
distributed/sharded database and they're not cluster-aware to redirect & load balance reads/writes
to the cluster. See [cluster aware drivers](#cluster-aware-drivers) that we've implemented.

For all scenarios, there are several solutions using proxies & load-balancers that 
you can use and are tested in production:

1. [haproxy](http://www.haproxy.org/) load balancer. Can be used with [pgsql-check](http://cbonte.github.io/haproxy-dconv/2.2/configuration.html#option%20pgsql-check) 
plugin which periodically checks if servers are up and adds/removes them from the pool.
2. [pgbouncer](https://www.pgbouncer.org/) proxy
3. [pgbouncer-rr-patch](https://github.com/awslabs/pgbouncer-rr-patch) is a fork of pgbouncer
from Amazon AWS with added functionality of programmable Query Routing & Rewriting.
4. Custom logic in your code
You can keep a list of servers on the client, have a connection pool for each client and use them
randomly or by a defined logic. You can consult our [JDBC driver](https://github.com/yugabyte/jdbc-yugabytedb) 
for implementation details.
5. [odyssey pooler](https://github.com/yandex/odyssey) as connection pooler/proxy.
See [comparisons](https://github.com/yandex/odyssey/issues/3) to pgbouncer.
6. [consul](https://www.consul.io/) can be used for service discovery of the tserver nodes
On the client node the consul agent will provide a hostname which will point to a 
server that is up. You can use that hostname in the connection string to YugabyteDB.
This way you keep only 1 connection pool to 1 server. When the server goes down,
consul will return a new hostname which will be able to serve queries.
7. [nginx](http://nginx.org/) can be used as a tcp proxy & load balancer in front
of a cluster of YugabyteDB.
8. When using kubernetes, YugabyteDB uses it's load balancer to proxy the tservers.
9. In GCP you can use [Cloud Load Balancer](https://cloud.google.com/load-balancing/) as TCP proxy.
10. In AZURE you can use [Load Balancer](https://azure.microsoft.com/en-us/services/load-balancer/) service. 

{{< note title="Note" >}}
- Make sure to set forwarding rules to port :5433.
- Configure health checks using HTTP port :7000 and path `/status`.
- The load balancer is only needed for tservers. Clients don't need to connect to Masters.
{{< /note >}}

## Partial indexes
A partial index is an index that is built on a subset of a table and includes only rows that satisfy the condition 
specified in the `WHERE` clause. It can be used to exclude `NULL` or common values from the index. 
This will speed up any writes to the table since rows containing the common column values don't need to be indexed. 
It will also reduce the size of the index, thereby improving the speed for read queries that use the index.
It can also be used to index just the rows of interest. 
For example, an application maintaining a shipments table may have a column for shipment status. 
If the application mostly accesses the in-flight shipments, then it can use a partial index to exclude rows whose shipment status is delivered.

Partial indexes can also be combined with [Covering indexes](../api/ysql/commands/ddl_create_index.md#include-columns).

## Connection pooling
YugabyteDB uses the upper half of Postgresql to implement it's YSQL layer.
Connection in Postgresql fork a new process, have at least 10MB of overhead in RAM, 
have context-switching overhead and are generally slow to create. This is
usually mitigated by keeping connection poolers in the client code or in a 
 [proxy/loadbalancer](#ysql-proxy--load-balancer).

Because of each connections overhead, we have to be careful regarding the number
of opened connections that we keep for each tserver. Currently the maximum number 
per-tserver is 300 and is set by the `--ysql_max_connections` gflag.

We're also working to lower the overhead of connections.

## Use `TRUNCATE` to empty tables instead of `DELETE`
`TRUNCATE` deletes the database files that store the table and is very fast. 
While DELETE inserts a `delete marker` for each row  in transactions and they are removed from storage when a compaction 
runs.

## Use jsonb columns only when necessary
`jsonb` columns are slower to read/write compared to normal columns. 
They also take more space because they need to store keys in strings and make keeping data consistency harder 
(needing complex queries to update jsonb values). 
A good schema design is to keep most columns as regular ones and only using `jsonb` for truly dynamic values. 
Don't create a `data jsonb` column where you put everything, but a `dynamic_data jsonb` column and other ones being 
primitive columns.

## Covering indexes
When querying by a secondary index, the original table is consulted to get the columns that aren't specified in the 
index. This can result in multiple random reads across the main table and even network traffic if the main rows reside 
in another yb-tserver.

Sometimes a better way is to include the other columns that we're quering that aren't part of the index 
using the [`INCLUDE`](../api/ysql/commands/ddl_create_index.md#include-clause) clause.  
When additional columns are included in the index, they can be used to respond to queries directly from the index without querying the table.

This turns a (possible) random read from the main table to just a filter on the index.

## Multi tenant use cases
There are many cases where data is spread across many tenants. Usually each 
tenant is isolated with it's own data. In these cases users may be inclined to 
create per-tenant tables/databases.
Because each table/tablet has overhead, having a large number of them is not 
recommended.
Depending on the number of tenants, it may be better to have a `tenant_id` on 
primary-keys which can be used to filter tenants at write/query time.
This can be combined with moving large tenants to their own private tables.


## Number of tables
Each table is split into tablets and each tablet has overhead. 
See [tablets per server](#tablets-per-server) for limits.

## Tablets per server
Each table consists of several tablets based on the `yb_num_shards_per_tserver`
gflag. Each `tablet` has overhead in the memory of the tserver that it resides.
Currently we recommend a maximum of around `500` tablets on each tserver.
You have to keep this number in mind depending on the number of tables and number 
of tablets per-server that you intend to create.
We're [actively working](https://github.com/yugabyte/yugabyte-db/issues/1317) to increase this limit.

On deployments where we have many small tables and few big tables, we can use [colocation](../explore/colocated-tables/linux.md) in YSQL layer to group small 
tables into 1 tablet.


## Column size limit
For consistent latency/performance, we suggest keeping columns in the `2MB` range 
or less even though we support an individual column being about 32MB.

## Row size limit
Big columns add up when selecting full rows or multiple of them. 
For consistent latency/performance, we suggest keeping the size in the `32MB` range
or less. This is a combination of [column sizing recommendations](#column-size-limit) for all columns.

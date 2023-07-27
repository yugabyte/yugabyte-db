---
title: Sharding data
headerTitle: Sharding data across nodes
linkTitle: Sharding data across nodes
description: Sharding data across nodes in YugabyteDB
headcontent: Hash and range sharding in YugabyteDB
menu:
  stable:
    name: Sharding data
    identifier: explore-transactions-sharding-data-1-ysql
    parent: explore-scalability
    weight: 210
type: docs
rightNav:
  hideH4: true
---

<!--
<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../scaling-transactions/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../distributed-transactions-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>
-->

YugabyteDB automatically splits user tables into multiple shards, called tablets, using either a [hash](#hash-sharding)- or [range](#range-sharding)-based strategy.

The primary key for each row in the table uniquely identifies the location of the tablet in the row, as per the following diagram:

![Sharding a table into tablets](/images/architecture/partitioning-table-into-tablets.png)

By default, YugabyteDB creates one tablet per node in the universe for each table and automatically distributes the data across them. In the [Examples](#Examples), you explore how automatic sharding is done internally for tables. The system Redis table works in exactly the same way.

## Sharding strategies

Sharding is the process of breaking up large tables into smaller chunks called shards spread across multiple servers. Essentially, a shard is a horizontal data partition that contains a subset of the total data set, and hence is responsible for serving a portion of the overall workload. The idea is to distribute data that cannot fit on a single node onto a cluster of database nodes.

Sharding is also referred to as horizontal partitioning. The distinction between horizontal and vertical comes from the traditional tabular view of a database. A database can be split vertically, storing different table columns in a separate database, or horizontally, storing rows of the same table in multiple database nodes.

YugabyteDB currently supports two ways of sharding data: [hash](#hash-sharding) (also known as consistent hash) sharding, and [range](#range-sharding) sharding.

For additional information about sharding in YugabyteDB, see the following:

- [Architecture: sharding](../../../architecture/docdb-sharding/)
- [Data sharding in a distributed SQL database](https://www.yugabyte.com/blog/how-data-sharding-works-in-a-distributed-sql-database/)
- [Analysis of four data sharding strategies in a distributed SQL database](https://www.yugabyte.com/blog/four-data-sharding-strategies-we-analyzed-in-building-a-distributed-sql-database/)
- [Overcoming MongoDB sharding and replication limitations with YugabyteDB](https://www.yugabyte.com/blog/overcoming-mongodb-sharding-and-replication-limitations-with-yugabyte-db/)

### Hash sharding

With consistent hash sharding, a sharding algorithm distributes data evenly and randomly across shards. The algorithm places each row of the table into a shard determined by computing a consistent hash on the hash column values of that row.

The hash space for hash-sharded YugabyteDB tables is the 2-byte range from `0x0000` to `0xFFFF`. A table may therefore have at most 65,536 tablets. This is expected to be sufficient in practice even for very large data sets or cluster sizes.

For example, for a table with 16 tablets the overall hash space of 0x0000 to 0xFFFF is divided into 16 sub-ranges, one for each tablet: 0x0000 to 0x1000, 0x1000 to 0x2000, and so on up to 0xF000 to 0xFFFF. Read and write operations are processed by converting the primary key into an internal key and its hash value, and determining the tablet to which the operation should be routed.

Hash sharding is ideal for massively scalable workloads, as it distributes data evenly across all the nodes in the universe while retaining ease of adding nodes into the cluster.

With consistent hash sharding, there are many more shards than the number of nodes and there is an explicit mapping table maintained tracking the assignment of shards to nodes. When adding new nodes, a subset of shards from existing nodes can be efficiently moved into the new nodes without requiring a massive data reassignment.

A potential downside of hash sharding is that performing range queries could be inefficient. Examples of range queries are finding rows greater than a lower bound or less than an upper bound (as opposed to point lookups).

#### Syntax example

In [YSQL](../../../api/ysql/the-sql-language/statements/ddl_create_table/#primary-key), to create a table with hash sharding, you specify `HASH` for the primary key. For example:

```sql
CREATE TABLE customers (
    customer_id bpchar NOT NULL,
    company_name character varying(40) NOT NULL,
    contact_name character varying(30),
    contact_title character varying(30),
    address character varying(60),
    city character varying(15),
    region character varying(15),
    postal_code character varying(10),
    country character varying(15),
    phone character varying(24),
    fax character varying(24),
    PRIMARY KEY (customer_id HASH)
);
```

In [YCQL](../../../api/ycql/ddl_create_table/), you can only create tables with hash sharding, so an explicit syntax for setting hash sharding is not necessary:

```sql
CREATE TABLE items (
    supplier_id INT,
    item_id INT,
    supplier_name TEXT STATIC,
    item_name TEXT,
    PRIMARY KEY((supplier_id), item_id)
);
```

### Range sharding

Range sharding involves splitting the rows of a table into _contiguous ranges_, based on the primary key column values. Range-sharded tables usually start out with a single shard. As data is inserted into the table, it is dynamically split into multiple shards because it is not always possible to know the distribution of keys in the table ahead of time.

This type of sharding allows efficiently querying a range of rows by the primary key values. Examples of such a query is to look up all keys that lie between a lower bound and an upper bound.

Range sharding has a few issues at scale, including:

- Starting out with a single shard means that a single node is handling all user queries.

    This often results in a database "warming" problem, where all queries are handled by a single node even if there are multiple nodes in the cluster. The user would have to wait for enough splits to happen and these shards to get redistributed before all nodes in the cluster are being utilized. This can be a big issue in production workloads. This can be mitigated in some cases where the distribution is keys is known ahead of time by pre-splitting the table into multiple shards, however this is hard in practice.

- Globally ordering keys across all the shards often generates hot spots, in which some shards get much more activity than others.

    Nodes hosting hot spots are overloaded relative to others. You can mitigate this to some extent with active load balancing, but this does not always work well in practice: by the time hot shards are redistributed across nodes, the workload may have changed and introduced new hot spots.

#### Syntax example

In [YSQL](../../../api/ysql/the-sql-language/statements/ddl_create_table/#primary-key), to create a table with range sharding, use the `ASC` or `DESC` keywords. For example:

```sql
CREATE TABLE order_details (
    order_id smallint NOT NULL,
    product_id smallint NOT NULL,
    unit_price real NOT NULL,
    quantity smallint NOT NULL,
    discount real NOT NULL,
    PRIMARY KEY (order_id ASC, product_id),
    FOREIGN KEY (product_id) REFERENCES products,
    FOREIGN KEY (order_id) REFERENCES orders
);
```

In YCQL, you can't create tables with range sharding. YCQL tables are always hash sharded.

## Examples

This example shows how automatic sharding works in YugabyteDB. First, you create some tables to understand how automatic sharding works. Then, you insert entries one by one, and examine how the data gets distributed across the various nodes.

This tutorial uses the [yugabyted](../../../reference/configuration/yugabyted/) cluster management utility.

### Create a universe

To create a universe, do the following:

1. Create a single-node universe by running the following command:

    ```sh
    ./bin/yugabyted start \
                     --base_dir=/tmp/ybd1 \
                     --listen=127.0.0.1 \
                     --tserver_flags "memstore_size_mb=1"
    ```

    `memstore_size_mb=1` sets the total size of memstores on the tablet-servers to `1MB`. This forces a flush of the data to disk when a value greater than 1MB is added, allowing you to observe to which tablets the data is written.

1. Add two more nodes to make this a 3-node by joining them with the previous node. You need to pass the `memstore_size` flag to each of the added YB-TServer servers, as follows:

    ```sh
    ./bin/yugabyted start \
                      --base_dir=/tmp/ybd2 \
                      --listen=127.0.0.2 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

    ```sh
    ./bin/yugabyted start \
                      --base_dir=/tmp/ybd3 \
                      --listen=127.0.0.3 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

Note that setting `memstore_size` to such a low value is not recommended in production, and is only used in the preceding example to force flushes to happen more quickly.

### Create a table

After you have got your nodes set up, you can create a YCQL table. Because you are using a workload application in the [YugabyteDB workload generator](https://github.com/yugabyte/yb-sample-apps) to write data into this table, create the keyspace and table name as follows:

```sh
./bin/ycqlsh
```

```sql
ycqlsh> CREATE KEYSPACE ybdemo_keyspace;
```

```sql
ycqlsh> CREATE TABLE ybdemo_keyspace.cassandrakeyvalue (k text PRIMARY KEY, v blob);
```

By default, [yugabyted](../../../reference/configuration/yugabyted/) creates one tablet per node per table. So for a three-node universe, three tablets are created for the preceding table, one on every node. Every such tablet is replicated three times for fault tolerance, so that makes the total number of tablets to be 3*3=9. Every node thus contains three tablets, one of which is the leader and the remaining two are the followers.

### Explore tablets

Using the universe Admin UI, you can observe the following:

- The tablets are evenly balanced across the various nodes. You can see the number of tablets per node in the **Tablet Servers** page of the master admin UI by navigating to the [table details page](http://127.0.0.1:7000/tablet-servers) that should look similar to the following:

    ![Number of tablets in the table](/images/ce/sharding_evenload.png)

    Notice that each node has three tablets, and the total number of tablets is nine, as expected. Out of these three, it is the leader of one and follower of other two.

- The table has three shards, each owning a range of the keyspace. Navigate to the [table details page](http://127.0.0.1:7000/table?keyspace_name=ybdemo_keyspace&table_name=cassandrakeyvalue) to examine various tablets and expect to see the following:

    ![Tablet details of the table](/images/ce/sharding_keyranges.png)

    There are three shards as expected, and the key ranges owned by each tablet are shown. This page also shows which node is currently hosting and is the leader for each tablet. Note that tablet balancing across nodes happens on a per-table basis, with each table scaled out to an appropriate number of nodes.

- Each tablet has a separate directory dedicated to it for data. List all the tablet directories and check their sizes, as follows:

    1. Get the table ID of the table you created by navigating to the [table listing page](http://127.0.0.1:7000/tables) and accessing the row corresponding to `ybdemo_keyspace.cassandrakeyvalue`. In this illustration, the table ID is `769f533fbde9425a8520b9cd59efc8b8`.

        ![Id of the created table](/images/ce/sharding_tableid.png)

    1. View all the tablet directories and their sizes for this table by running the following command, replacing the ID with your ID:

        ```sh
        du -hs /tmp/ybd*/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/* | grep -v '0B'
        ```

        ```output
        28K /tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
        28K /tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
        28K /tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
        28K /tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
        28K /tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
        28K /tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
        28K /tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
        28K /tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
        28K /tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
        ```

        There are nine entries, one corresponding to each tablet with 28K bytes as the size.

### Insert and query table

You can use the sample application to insert a key-value entry with the value size around 10MB. Because the memstores are configured to be 1MB, this causes the data to flush to disk immediately.

The following are the key flags that you pass to the sample application:

- `--num_unique_keys 1` to write exactly one key. Keys are numbers converted to text, and typically start from 0.
- `--num_threads_read 0` to not perform any reads (hence 0 read threads).
- `--num_threads_write 1` creates one writer thread. Because you're not writing a lot of data, a single writer thread is sufficient.
- `--value_size 10000000` to generate the value being written as a random byte string of around 10MB size.
- `--nouuid` to not prefix a UUID to the key. A UUID allows multiple instances of the load tester to run without interfering with each other.

Perform the following:

1. Download the YugabyteDB workload generator JAR file (`yb-sample-apps.jar`) as follows:

    {{% yb-sample-apps-path %}}

1. Run the `CassandraKeyValue` workload application, as follows:

    ```sh
    java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
                                        --nodes 127.0.0.1:9042 \
                                        --nouuid \
                                        --num_unique_keys 1 \
                                        --num_writes 2 \
                                        --num_threads_read 0 \
                                        --num_threads_write 1 \
                                        --value_size 10000000
    ```

    ```output
    0 [main] INFO com.yugabyte.sample.Main  - Starting sample app...
    ...
    38 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Num unique keys to insert: 1
    38 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Num keys to update: 1
    38 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Num keys to read: -1
    38 [main] INFO com.yugabyte.sample.common.CmdLineOpts  - Value size: 10000000
    ...
    4360 [main] INFO com.yugabyte.sample.Main  - The sample app has finished
    ```

1. Use `ycqlsh` to check what you have inserted:

    ```sh
    ./bin/ycqlsh
    ```

    ```sql
    ycqlsh> SELECT k FROM ybdemo_keyspace.cassandrakeyvalue;
    ```

    ```output
    k
    -------
    key:0

    (1 rows)
    ```

1. Check the sizes of the various tablets, as follows:

    ```sh
    du -hs /tmp/ybd*/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/* | grep -v '0B'
    ```

    ```output
    28K   /tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    9.6M  /tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K   /tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    28K   /tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    9.6M  /tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K   /tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    28K   /tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    9.6M  /tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K   /tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    ```

You can see that the key was successfully inserted in one of the tablets leading to a proliferation of size to 9.6 MB. Because this tablet has three copies distributed across three nodes, you can see three entries of this size.

The key has been written to one of the tablets. In this example, the tablet's UUID is `0df2c7cd87a844c99172ea1ebcd0a3ee`. Check the [table details page](http://127.0.0.1:7000/table?keyspace_name=ybdemo_keyspace&table_name=cassandrakeyvalue) to determine to which node this tablet belongs; in this case, it is `node-1`.

![Tablet ownership with auto-sharding](/images/ce/sharding_tablet.png)

### Observe automatic sharding when adding nodes

1. Add one more node to the universe for a total of four nodes, as follows:

    ```sh
    ./bin/yugabyted start \
                      --base_dir=/tmp/ybd4 \
                      --listen=127.0.0.4 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

1. Check the tablet server's page to see that the tablets are redistributed evenly among the four nodes, as per the following illustration:

    ![Auto-sharding when adding one node](/images/ce/sharding_4nodes.png)

1. Add two more nodes to the universe, making it a total of six nodes, as follows:

    ```sh
    ./bin/yugabyted start \
                      --base_dir=/tmp/ybd5 \
                      --listen=127.0.0.5 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

    ```sh
    ./bin/yugabyted start \
                      --base_dir=/tmp/ybd5 \
                      --listen=127.0.0.6 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

1. Verify that the tablets are evenly distributed across the six nodes. Each node now has two tablets, as per the following illustration:

    ![Auto-sharding when adding three nodes](/images/ce/sharding_6nodes.png)

{{% explore-cleanup-local %}}

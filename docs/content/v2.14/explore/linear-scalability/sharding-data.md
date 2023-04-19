---
title: Sharding Data
headerTitle: Sharding Data Across Nodes
linkTitle: Sharding Data Across Nodes
description: Sharding Data Across Nodes in YugabyteDB
headcontent: Sharding Data Across Nodes in YugabyteDB
menu:
  v2.14:
    name: Sharding Data
    identifier: explore-transactions-sharding-data-1-ysql
    parent: explore-scalability
    weight: 210
type: docs
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

YugabyteDB automatically splits user tables into multiple shards, called *tablets*, using either a [hash](#hash-sharding)- or [range](#range-sharding)- based strategy.

The primary key for each row in the table uniquely determines the tablet the row lives in. This is shown in the following figure.

![Sharding a table into tablets](/images/architecture/partitioning-table-into-tablets.png)

By default, YugabyteDB creates eight tablets per node in the cluster for each table and automatically distributes the data across the various tablets, which in turn are distributed evenly across the nodes. In the [Try it out](#try-it-out) section, you'll explore how automatic sharding is done internally for tables. The system Redis table works in exactly the same way.

## Sharding strategies

Sharding is the process of breaking up large tables into smaller chunks called _shards_ that are spread across multiple servers. Essentially, a shard is a _horizontal data partition_ that contains a subset of the total data set, and hence is responsible for serving a portion of the overall workload. The idea is to distribute data that can’t fit on a single node onto a cluster of database nodes.

Sharding is also referred to as _horizontal partitioning_. The distinction between horizontal and vertical comes from the traditional tabular view of a database. A database can be split vertically&mdash;storing different table columns in a separate database, or horizontally&mdash;storing rows of the same table in multiple database nodes.

YugabyteDB currently supports two ways of sharding data: [hash](#hash-sharding) (also known as _consistent hash_) sharding, and [range](#range-sharding) sharding.

### Hash sharding

With (consistent) hash sharding, a sharding algorithm distributes data evenly and randomly across shards. The algorithm places each row of the table into a shard determined by computing a consistent hash on the hash column values of that row.

The hash space for hash-sharded YugabyteDB tables is the 2-byte range from `0x0000` to `0xFFFF`. A table may therefore have at most 65,536 tablets. This is expected to be sufficient in practice even for very large data sets or cluster sizes.

For example, for a table with 16 tablets the overall hash space of 0x0000 to 0xFFFF is divided into 16 sub-ranges, one for each tablet: 0x0000 to 0x1000, 0x1000 to 0x2000, and so on up to 0xF000 to 0xFFFF. Read and write operations are processed by converting the primary key into an internal key and its hash value, and determining the tablet to which the operation should be routed.

**Hash sharding is ideal for massively scalable workloads**, as it distributes data evenly across all the nodes in the cluster, while retaining ease of adding nodes into the cluster.

With consistent hash sharding, there are many more shards than the number of nodes and there is an explicit mapping table maintained tracking the assignment of shards to nodes. When adding new nodes, a subset of shards from existing nodes can be efficiently moved into the new nodes without requiring a massive data reassignment.

**A potential downside** of hash sharding is that performing range queries could be inefficient. Examples of range queries are finding rows greater than a lower bound or less than an upper bound (as opposed to point lookups).

#### Hash sharding example

**In YSQL**, create a table with hash sharding:

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

**In YCQL**, you can only create tables with hash sharding, so an explict syntax for setting hash sharding is not necessary.

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

**Range sharding has a few issues at scale**, including:

* Starting out with a single shard means that _a single node is handling all user queries_.

    <br/><br/>
    This often results in a database “warming” problem, where all queries are handled by a single node even if there are multiple nodes in the cluster. The user would have to wait for enough splits to happen and these shards to get redistributed before all nodes in the cluster are being utilized. This can be a big issue in production workloads. This can be mitigated in some cases where the distribution is keys is known ahead of time by presplitting the table into multiple shards, however this is hard in practice.

* Globally ordering keys across all the shards often generates hot spots, in which some shards get much more activity than others.

    <br/><br/>
    Nodes hosting hot spots are overloaded relative to others. You can mitigate this to some extent with active load balancing, but this does not always work well in practice: by the time hot shards are redistributed across nodes, the workload may have changed and introduced new hot spots.

#### Range sharding example

**In YSQL**, create a table with range sharding:

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

**In YCQL**, you can't create tables with range sharding. YCQL tables are always hash sharded.

## Try it out

In this tutorial, you'll explore automatic sharding inside YugabyteDB. First, you'll create some tables to understand how automatic sharding works. Then, you'll insert entries one by one, and examine how the data gets distributed across the various nodes.

This tutorial uses the [yugabyted](../../../reference/configuration/yugabyted/) cluster management utility.

### Create a universe

To create a universe, do the following:

1. Let’s begin by creating a single node cluster. Run the following command.

   ```sh
   $ ./bin/yugabyted start \
                     --base_dir=/tmp/ybd1 \
                     --listen=127.0.0.1 \
                     --tserver_flags "memstore_size_mb=1"
   ```

   * `memstore_size_mb=1` sets the total size of memstores on the tablet-servers to `1MB`. This will force a flush of the data to disk when a value greater than 1MB is added, so that you can observe which tablets the data is written to.

1. Add two more nodes to make this a 3-node by joining them with the previous node. You need to pass the memstore_size flag to each of the added YB-TServer servers.

    ```sh
    $ ./bin/yugabyted start \
                      --base_dir=/tmp/ybd2 \
                      --listen=127.0.0.2 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

    ```sh
    $ ./bin/yugabyted start \
                      --base_dir=/tmp/ybd3 \
                      --listen=127.0.0.3 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

{{< warning title="Warning" >}}
Setting `memstore_size` to such a low value is not recommended in production, and is only being used here to illustrate the point by forcing flushes to happen more quickly.
{{< /warning >}}

### Create a table

Once you've got your nodes set up, you can create a YCQL table. Since you'll be using a workload application in the [YugabyteDB workload generator](https://github.com/yugabyte/yb-sample-apps) to write data into this table, **create the keyspace and table name below exactly as shown**.

```sh
$ ./bin/ycqlsh
```

```sql
ycqlsh> CREATE KEYSPACE ybdemo_keyspace;
```

```sql
ycqlsh> CREATE TABLE ybdemo_keyspace.cassandrakeyvalue (k text PRIMARY KEY, v blob);
```

By default, [yugabyted](../../../reference/configuration/yugabyted/) creates one tablet per node per table. So for a 3 node cluster, 3 tablets are created for the above table; one on every node. Every such tablet is replicated 3 times for fault tolerance, so that makes the total number of tablets to be 3*3=9. Every node thus contains 3 tablets, one of which it is the leader and the remaining 2 of which it is the follower.

### Explore tablets

* **The tablets are evenly balanced across the various nodes**. You can see the number of tablets per node in the Tablet Servers page of the master Admin UI, by going to the [table details page](http://127.0.0.1:7000/tablet-servers). The page should look something like the image below.

    ![Number of tablets in the table](/images/ce/sharding_evenload.png)

    Notice that each node has 3 tablets, and the total number of tablets is 9 as expected. Out of these 3, it is the leader of 1 and follower of other 2.

* **The table has 3 shards, each owning a range of the keyspace**. Navigate to the [table details page](http://127.0.0.1:7000/table?keyspace_name=ybdemo_keyspace&table_name=cassandrakeyvalue) to examine the various tablets. This page should look as follows.

    ![Tablet details of the table](/images/ce/sharding_keyranges.png)

    There are 3 shards as expected, and the key ranges owned by each tablet are shown. This page also shows which node is currently hosting (and is the leader for) each tablet. Note here that tablet balancing across nodes happens on a per-table basis, with each table scaled out to an appropriate number of nodes.

* **Each tablet has a separate directory dedicated to it for data**. List out all the tablet directories and check their sizes, as follows:

1. First get the table-id of the table you created by going to the [table listing page](http://127.0.0.1:7000/tables) and accessing the row corresponding to `ybdemo_keyspace.cassandrakeyvalue`. In this illustration, the table-id is `769f533fbde9425a8520b9cd59efc8b8`.

    ![Id of the created table](/images/ce/sharding_tableid.png)

1. Next, you can view all the tablet directories and their sizes for this table by running the following command. Remember to replace the id with your corresponding id.

    ```sh
    $ du -hs /tmp/ybd*/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/* | grep -v '0B'
    ```

    ```output
    28K	/tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    28K	/tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K	/tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    28K	/tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    28K	/tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K	/tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    28K	/tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    28K	/tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K	/tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    ```

    There are nine entries, one corresponding to each tablet with 28K bytes as the size.

### Insert and query a table

In this section, you'll use a sample app to insert a key-value entry with the value size around 10MB. Since the memstores are configured to be 1MB, this causes the data to flush to disk immediately.

The key flags you pass to the sample app are:

* `--num_unique_keys 1` to write exactly one key. Keys are numbers converted to text, and typically start from 0.
* `--num_threads_read 0` to not perform any reads (hence 0 read threads).
* `--num_threads_write 1` creates one writer thread. Since you're not writing a lot of data, a single writer thread is sufficient.
* `--value_size 10000000` to generate the value being written as a random byte string of around 10MB size.
* `--nouuid` to not prefix a UUID to the key. A UUID allows multiple instances of the load tester to run without interfering with each other.

Let's get started:

1. Download the YugabyteDB workload generator JAR file (`yb-sample-apps.jar`):

    ```sh
    $ wget https://github.com/yugabyte/yb-sample-apps/releases/download/1.3.9/yb-sample-apps.jar
    ```

1. Run the `CassandraKeyValue` workload application.

    ```sh
    $ java -jar ./yb-sample-apps.jar --workload CassandraKeyValue \
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

1. Check what you've inserted using `ycqlsh`.

    ```sh
    $ ./bin/ycqlsh
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

1. Next, check the sizes of the various tablets:

    ```sh
    $ du -hs /tmp/ybd*/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/* | grep -v '0B'
    ```

    ```output
    28K	/tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    9.6M    /tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K	/tmp/ybd1/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    28K	/tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    9.6M    /tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K	/tmp/ybd2/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    28K	/tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0149071638294c5d9328c4121ad33d23
    9.6M    /tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-0df2c7cd87a844c99172ea1ebcd0a3ee
    28K	/tmp/ybd3/data/yb-data/tserver/data/rocksdb/table-769f533fbde9425a8520b9cd59efc8b8/tablet-59b7d53724944725a1e3edfe9c5a1440
    ```

You can see that the key was successfully inserted in one of the tablets leading to a proliferation of size to 9.6 MB. Because this tablet has 3 copies distributed across the 3 nodes, you can see 3 entries of this size.

Here, the key has been written to one of the tablets. In this example, the tablet's UUID is `0df2c7cd87a844c99172ea1ebcd0a3ee`. Check the [table details page](http://127.0.0.1:7000/table?keyspace_name=ybdemo_keyspace&table_name=cassandrakeyvalue) to determine which node this tablet belongs to, and in this case it's `node-1`.

![Tablet ownership with auto-sharding](/images/ce/sharding_tablet.png)

### Automatic sharding when adding nodes

1. Add one more node to the universe for a total of 4 nodes:

    ```sh
    $ ./bin/yugabyted start \
                      --base_dir=/tmp/ybd4 \
                      --listen=127.0.0.4 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

1. Check the tablet servers page, to see that the tablets are re-distributed evenly among the 4 nodes:

    ![Auto-sharding when adding one node](/images/ce/sharding_4nodes.png)

1. Add 2 more nodes to the universe, making it a total of 6 nodes:

    ```sh
    $ ./bin/yugabyted start \
                      --base_dir=/tmp/ybd5 \
                      --listen=127.0.0.5 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

    ```sh
    $ ./bin/yugabyted start \
                      --base_dir=/tmp/ybd5 \
                      --listen=127.0.0.5 \
                      --join=127.0.0.1 \
                      --tserver_flags "memstore_size_mb=1"
    ```

1. Verify that the tablets are evenly distributed across the 6 nodes. Each node now has 2 tablets.

    ![Auto-sharding when adding three nodes](/images/ce/sharding_6nodes.png)

### [Optional] Clean up

If you're done experimenting, run the following commands to shut down the local cluster:

```sh
$ ./bin/yugabyted destroy \
                  --base_dir=/tmp/ybd1
```

```sh
$ ./bin/yugabyted destroy \
                  --base_dir=/tmp/ybd2
```

```sh
$ ./bin/yugabyted destroy \
                  --base_dir=/tmp/ybd3
```

```sh
$ ./bin/yugabyted destroy \
                  --base_dir=/tmp/ybd4
```

```sh
$ ./bin/yugabyted destroy \
                  --base_dir=/tmp/ybd5
```

```sh
$ ./bin/yugabyted destroy \
                  --base_dir=/tmp/ybd6
```

## Further reading

To learn more about sharding in YugabyteDB, you may wish to read the [architecture documentation](../../../architecture/docdb-sharding/), or the following blog posts:

[How Data Sharding Works in a Distributed SQL Database](https://www.yugabyte.com/blog/how-data-sharding-works-in-a-distributed-sql-database/)

[Four Data Sharding Strategies We Analyzed in Building a Distributed SQL Database](https://www.yugabyte.com/blog/four-data-sharding-strategies-we-analyzed-in-building-a-distributed-sql-database/)

[Overcoming MongoDB Sharding and Replication Limitations with YugabyteDB](https://www.yugabyte.com/blog/overcoming-mongodb-sharding-and-replication-limitations-with-yugabyte-db/)

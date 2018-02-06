## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```sh
./bin/yb-ctl destroy
```


Start a new local universe with a replication factor of 1 (rf=1). We are passing the following options/flags:

- `--replication_factor 1` This creates a universe with a replication factor of 1.
- `--num_shards_per_tserver 4`  This option controls the total number of tablets (or partitions) when creating a new table. By making this number 4, we will end up creating 12 tablets on a 3 node cluster. 
- `--tserver_flags "memstore_size_mb=1"` This sets the total size of memstores on the tablet-servers to 1MB. We do this in order to force a flush of the data to disk when we insert a value more than 1MB, so that we can observe which tablets the data gets written to.

```sh
./bin/yb-ctl --replication_factor 1 --num_shards_per_tserver 4 create \
             --tserver_flags "memstore_size_mb=1"
```


The above command creates a universe with one node. Let us add 2 more nodes to make this a 3-node, rf=1 universe. We need to pass the memstore size flag to each new tserver we add. You can do that by running the following:

```sh
./bin/yb-ctl add_node --tserver_flags "memstore_size_mb=1"
./bin/yb-ctl add_node --tserver_flags "memstore_size_mb=1"
```

We can check the status of the cluster to confirm that we have 3 tservers.

```sh
$ ./bin/yb-ctl status
2018-02-03 21:33:05,455 INFO: Server is running: type=master, node_id=1, PID=18967, admin service=http://127.0.0.1:7000
2018-02-03 21:33:05,477 INFO: Server is running: type=tserver, node_id=1, PID=18970, admin service=http://127.0.0.1:9000, cql service=127.0.0.1:9042, redis service=127.0.0.1:6379
2018-02-03 21:33:05,499 INFO: Server is running: type=tserver, node_id=2, PID=19299, admin service=http://127.0.0.2:9000, cql service=127.0.0.2:9042, redis service=127.0.0.2:6379
2018-02-03 21:33:05,523 INFO: Server is running: type=tserver, node_id=3, PID=19390, admin service=http://127.0.0.3:9000, cql service=127.0.0.3:9042, redis service=127.0.0.3:6379
```


## 2. Create a Cassandra table


Create a Cassandra table. The keyspace and table name below must created exactly as shown below, since we will be using the sample application to write data into this table.

```sh
./bin/cqlsh
cqlsh> CREATE KEYSPACE ybdemo_keyspace;
cqlsh> CREATE TABLE ybdemo_keyspace.cassandrakeyvalue (k text PRIMARY KEY, v blob);
```


For each table, we have instructed YugaByte DB to create 4 shards per tserver present in the universe. Since we have 3 nodes, we expect 12 tablets for the `ybdemo_keyspace.cassandrakeyvalue` table.


## 3. Explore tablets

- The tablets are evenly balanced across the various nodes.

You can see the number of tablets per node in the Tablet Servers page of the master Admin UI, by going to the [table details page](http://127.0.0.1:7000/tablet-servers). The page should look something like the image below.

![Number of tablets in the Cassandra table](/images/explore/auto-sharding-cassandra-table-1.png)

We see that each node has 4 tablets, and the total number of tablets is 12 as we expected.

- The table has 12 tablets, each owning a range of the keyspace.

Let us navigate to the [table details page](http://127.0.0.1:7000/table?keyspace_name=ybdemo_keyspace&table_name=cassandrakeyvalue) to examine the various tablets. This page should look as follows.

![Tablet details of the Cassandra table](/images/explore/auto-sharding-cassandra-tablets.png)

What we see here is that there are 12 tablets as expected, and the key ranges owned by each tablet are shown. This page also shows which node that is currently hosting (and is the leader for) each of these tablets. Note here that the tablets balancing across nodes happens on a per-table basis, so that each table is scaled out to an appropriate number of nodes.

- Each tablet has a separate directory dedicated to it for data.

Let us list out all the tablet directories and see their sizes. This can be done as follows.

```sh
$ du -hs /tmp/yugabyte-local-cluster/node*/disk*/yb-data/tserver/data/rocksdb/table*/* | grep -v '0B'

 20K    /tmp/yugabyte-local-cluster/node-1/disk-1/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-439ae3bde90049d6812e198e76ad29a4
 20K    /tmp/yugabyte-local-cluster/node-1/disk-1/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-eecd01f0a7cd4537ba571bdb85d0c094
 20K    /tmp/yugabyte-local-cluster/node-1/disk-2/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-4ea334056a3845518cc6614baef96966
 20K    /tmp/yugabyte-local-cluster/node-1/disk-2/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-52642a3a9d7b4d38a103dff5dd77a3c6
 20K    /tmp/yugabyte-local-cluster/node-2/disk-1/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-4e31e26b3b204e34a1e0cfd6f7500525
 20K    /tmp/yugabyte-local-cluster/node-2/disk-1/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-b7ac08a983aa45a3843ab92b1719799a
 20K    /tmp/yugabyte-local-cluster/node-2/disk-2/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-22c349a07afb48e3844b570c24455431
 20K    /tmp/yugabyte-local-cluster/node-2/disk-2/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-8955db9e1ec841f3a30535b77d707586
 20K    /tmp/yugabyte-local-cluster/node-3/disk-1/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-adac9f92466b4d288a4ae346aaad3880
 20K    /tmp/yugabyte-local-cluster/node-3/disk-1/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-f04a6d5113a74ba79a04f01c80423ef5
 20K    /tmp/yugabyte-local-cluster/node-3/disk-2/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-1c472c1204fe40afbc7948dadce22be8
 20K    /tmp/yugabyte-local-cluster/node-3/disk-2/yb-data/tserver/data/rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-5aaeb96381044aa2b09ed9973830bb27
 ```

## 4. Insert/query the Cassandra table

Let us insert a key-value entry, with the value size around 2MB. Since the memstores are configured to be 1MB, this will cause the data to flush to disk immediately. Note that the key flags we pass to the sample app are:

- `--num_unique_keys 1` - Write exactly one key. Keys are numbers converted to text, and typically start from 0.
- `--num_threads_read 0` - Do not perform any reads (hence 0 read threads).
- `--num_threads_write 1` - Since we are not writing a lot of data, a single writer thread is sufficient.
- `--value_size 10000000`  - Generate the value being written as a random byte string of around 10MB size.
- `--nouuid` - Do not prefix a UUID to the key. A UUID allows multiple instances of the load tester to run without interfering with each other.

```sh
$ java -jar java/yb-sample-apps.jar --workload CassandraKeyValue \
                                    --nodes 127.0.0.1:9042 \
                                    --nouuid \
                                    --num_unique_keys 1 \
                                    --num_writes 2 \
                                    --num_threads_read 0 \
                                    --num_threads_write 1 \
                                    --value_size 10000000

2018-02-05 07:33:33,525 [INFO|...] Num unique keys to insert: 1
...
2018-02-05 07:33:36,899 [INFO|...] The sample app has finished
```

Let us check what we have inserted using `cqlsh`.

```sh
./bin/cqlsh
cqlsh> SELECT k FROM ybdemo_keyspace.cassandrakeyvalue;

 k
-------
 key:0

(1 rows)
```

Now let us check the sizes of the various tablets:

```sh
$ du -hs /tmp/yugabyte-local-cluster/node*/disk*/yb-data/tserver/data/rocksdb/table*/* | grep -v '0B'
 20K    .../rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-439ae3bde90049d6812e198e76ad29a4
9.6M    .../rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-eecd01f0a7cd4537ba571bdb85d0c094
 20K    .../rocksdb/table-9987797012ce4c1c91782c25e7608c34/tablet-4ea334056a3845518cc6614baef96966
 ...
 ```

We see that the key has been written to one of the tablets, in the case of this experiment the UUID of the tablet is `eecd01f0a7cd4537ba571bdb85d0c094`. We can find out from the [table details page](http://127.0.0.1:7000/table?keyspace_name=ybdemo_keyspace&table_name=cassandrakeyvalue) which node this tablet belongs to - it is `node-1` in this case. Here is the relevant screenshot.

![Tablet ownership with auto-sharding](/images/explore/auto-sharding-tablet-ownership.png)

We can also easily confirm that the `node-1` indeed has about 10MB of storage being used.

![Inserting values with auto-sharding](/images/explore/auto-sharding-single-kv-insert.png)


## 5. Automatic sharding when add nodes


Let us add one more node to the universe for a total of 4 nodes, by running the following command.

```sh
./bin/yb-ctl add_node --tserver_flags "memstore_size_mb=1"
```


By looking at the tablet servers page, we find that the tablets are re-distributed evenly among the 4 nodes and each node now has 3 shards.

![Auto sharding when adding one node](/images/explore/auto-sharding-add-1-node.png)

Next, let us add 2 more nodes to the universe, making it a total of 6 nodes. We can do this by running the following.

```sh
./bin/yb-ctl add_node --tserver_flags "memstore_size_mb=1"
./bin/yb-ctl add_node --tserver_flags "memstore_size_mb=1"
```

We can verify that the tablets are evenly distributed across the 6 nodes. Each node now has 2 tablets.

![Auto sharding when adding three nodes](/images/explore/auto-sharding-add-3-node.png)


## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```

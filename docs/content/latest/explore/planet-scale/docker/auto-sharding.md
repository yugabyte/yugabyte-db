## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl destroy
```
</div>

Start a new local universe with replication factor 1.
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl create --rf 1 
```
</div>

The above command creates a universe with one node. Let us add 2 more nodes to this universe. You can do that by running the following:
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl add_node
```
</div>
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl add_node
```
</div>


Create a CQL table. The keyspace and table name below must be named as shown below, since the sample application writes data to this table. We will use the sample application to write data to this table to understand sharding in a subsequent step.
<div class='copy separator-dollar'>
```sh
$ ./bin/cqlsh
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> CREATE KEYSPACE ybdemo_keyspace;
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> CREATE TABLE ybdemo_keyspace.cassandrakeyvalue (k text PRIMARY KEY, v blob);
```
</div>


## 2. Examine tablets

For each table, YugaByte creates 8 shards per node in the universe by default. In our example, since we have 3 nodes, we expect 24 tablets for each of the tables we created (the Redis and CQL tables), or 48 tablets total.

You can see the number of tablets per node in the Tablet Servers page of the master Admin UI, by going to http://127.0.0.1:7000/tablet-servers. The page should look something like the image below:

You can also navigate to the table details for these two tables by going to <URL>. This page should look as follows.


Note here that the tablets balancing across nodes happens on a per-table basis, so that each table is scaled out to an appropriate number of nodes.


## 3. Insert/query the Redis table

## 4. Insert/query the Cassandra table

## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl destroy
```
</div>

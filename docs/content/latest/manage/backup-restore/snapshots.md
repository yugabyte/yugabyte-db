---
title: Backing Up Data using Snapshots
linkTitle: Backing Up Data using Snapshots
description: Backing Up Data using Snapshots
image: /images/section_icons/manage/enterprise.png
headcontent: Backing up data using Snapshots
aliases:
  - manage/backup-restore/manage-snapshots
menu:
  latest:
    identifier: manage-backup-restore-manage-snapshots
    parent: manage-backup-restore
    weight: 705
isTocNested: true
showAsideToc: true
---

This page covers backups for YugabyteDB using snapshots. Here are some points to keep in mind.

- Distributed backups using snapshots
  - Massively parallel, efficient for very large data sets
  - Snapshot does a rocksdb flush and hardlinks the files in a `.snapshots` directory on each tablet
  - Snapshots are not transactional across the whole table but only on each tablet [#2086](https://github.com/YugaByte/yugabyte-db/issues/2086)
  - Multi table transactional snapshot is in the roadmap [#2084](https://github.com/YugaByte/yugabyte-db/issues/2084) 
  - Snapshoting is broken in YSQL [#2083](https://github.com/YugaByte/yugabyte-db/issues/2083)
  - The platform edition (enterprise) automates all this for you


In this tutorial we'll be using YCQL but the same apis are used in YSQL. 

### Step 1: Create a 1 node local cluster
Read [creating a local cluster](../../quick-start/create-local-cluster.md) on how to quickstart a a local cluster.

```
$ ./bin/yb-ctl create
Creating cluster.
Waiting for cluster to be ready.
----------------------------------------------------------------------------------------------------
| Node Count: 1 | Replication Factor: 1                                                            |
----------------------------------------------------------------------------------------------------
| JDBC                : postgresql://postgres@127.0.0.1:5433                                       |
| YSQL Shell          : bin/ysqlsh                                                                 |
| YCQL Shell          : bin/cqlsh                                                                  |
| YEDIS Shell         : bin/redis-cli                                                              |
| Web UI              : http://127.0.0.1:7000/                                                     |
| Cluster Data        : /home/guru/yugabyte-data                                                   |
----------------------------------------------------------------------------------------------------

For more info, please use: yb-ctl status
```

See [yb-ctl reference](../../admin/yb-ctl.md) for all options.

### Step 2: Creating a table with data
After [getting started on YCQL api](../../api/ycql/quick-start/) log into cqlsh:
```
$ ./bin/cqlsh
```

Create a keyspace, table & insert test data and verify we have data in the db by doing a simple select:
```
cqlsh> CREATE KEYSPACE ydb;
cqlsh> CREATE TABLE IF NOT EXISTS ydb.test_tb(user_id INT PRIMARY KEY);
cqlsh> INSERT INTO ydb.test_tb(user_id) VALUES (5);
cqlsh> SELECT * FROM ydb.test_tb;

 user_id
---------
       5

(1 rows)

```

### Step 3: Creating a snapshot

Create a snapshot from `yb-admin` binary:

```
$ ./bin/yb-admin create_snapshot ydb test_tb
Started flushing table ydb.test_tb
Flush request id: fe0db953a7a5416c90f01b1e11a36d24
Waiting for flushing...
Flushing complete: SUCCESS
Started snapshot creation: 4963ed18fc1e4f1ba38c8fcf4058b295
```

We can see when it finishes by listing snapshots:

```
$ ./bin/yb-admin list_snapshots
Snapshot UUID                    	State
4963ed18fc1e4f1ba38c8fcf4058b295 	COMPLETE
```

### Step 3.1: Exporting the snapshot

First we need to export a meta data file that describes the snapshot. 

```
$ ./bin/yb-admin export_snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 test_tb.snapshot
Exporting snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE) to file test_tb.snapshot
Snapshot meta data was saved into file: test_tb.snapshot
```

Then we need to copy the actual data from the table & tablets. In this case we 
have to use a script that copies all data. The filepath structure is:

```
<yb_data_dir>/node-<node_number>/disk-<disk_number>/yb-data/tserver/data/rocksdb/table-<table_id>/[tablet-<tablet_id>.snapshots]/<snapshot_id>
```

* `<yb_data_dir>` is the directory where YDB data is stored. (default=`~/yugabyte-data`)
* `<node_number>` is used when multiple nodes are running on the same server (usually in testing/qa/dev) (default=1)
* `<disk_number>` when running yugabyte on multiple disks with `--fs_data_dirs` flag (default=1)
* `<table_id>` is the UUID of the table (can take it from WEB UI)
* `<tablet_id>` in each table we have a list of tablets. And each tablet has a `<tablet_id>.snapshots` directory, which is what we really need to copy
* `<snapshot_id>` there is a directory for each snapshot since you can have multiple completed snapshots on each server

This directory structure is specific to `yb-ctl` which is a local testing tool. 
In practice, for each server, we will have an `--fs_data_dirs` flag, which is a csv of paths where to put the data (normally different paths should be on different disks).
In this `yb-ctl` example, these are the full paths up to the `disk-x`.


### Step 3.2: Copying snapshot data to another directory

{{< note title="Tip" >}}
When snapshoting a multi-node cluster, we need to go into each node and copy 
the folders of ONLY the leader tablets on that node. No need to keep a copy for each replica, since each tablet-replica will 
have the same data.
{{< /note >}}


First we need to get the `table_id` UUID that we're snapshoting. We can get this in 
the WEB UI [http://127.0.0.1:7000/tables](http://127.0.0.1:7000/tables) under "User Tables".

For each table, there are multiple tablets where the data is stored. We need to get a list of tablets and the Leader for each of them.

```
$ ./bin/yb-admin list_tablets ydb test_tb 0
Tablet UUID                      	Range                                                    	Leader
cea3aaac2f10460a880b0b4a2a4b652a 	partition_key_start: "" partition_key_end: "\177\377"    	127.0.0.1:9100
e509cf8eedba410ba3b60c7e9138d479 	partition_key_start: "\177\377" partition_key_end: ""    	127.0.0.1:9100
```
The third argument is for limiting the number of returned results. Setting it `0` returns all tablets.

Using this information we can construct the full path of all directories where snapshots are stored for each (tablet,snapshot_id).

We can create a small script to manually copy/move the folders to a backup directory/filesystem or external storage.

{{< note title="Tip" >}}
When doing RF1 as the source the output of the `yb-admin` like listing the tablets only shows LEADERS, because there's only 1 copy, which is the leader.
{{< /note >}}


### Step 4: Destroying cluster and creating a new one
First we destroy the cluster. 
```
$ ./bin/yb-ctl destroy
Destroying cluster.
```
And spin up a new one with 3 nodes in replicated setup.

```
./bin/yb-ctl --rf 3 create
Creating cluster.
Waiting for cluster to be ready.
----------------------------------------------------------------------------------------------------
| Node Count: 3 | Replication Factor: 3                                                            |
----------------------------------------------------------------------------------------------------
| JDBC                : postgresql://postgres@127.0.0.1:5433                                       |
| YSQL Shell          : bin/ysqlsh                                                                 |
| YCQL Shell          : bin/cqlsh                                                                  |
| YEDIS Shell         : bin/redis-cli                                                              |
| Web UI              : http://127.0.0.1:7000/                                                     |
| Cluster Data        : /home/guru/yugabyte-data                                                   |
----------------------------------------------------------------------------------------------------

For more info, please use: yb-ctl status
```
{{< note title="Tip" >}}
Make sure to get the master ip from `$ ./bin/yb-ctl status` since we have multiple nodes on different ips
{{< /note >}}

### Step 5: Triggering snapshot import
{{< note title="Tip" >}}
The `keyspace` and `table` can be different from the exported one.
{{< /note >}}
First we need to import the snapshot file into yugabyte.
```
$ ./bin/yb-admin import_snapshot test_tb.snapshot ydb test_tb
Read snapshot meta file test_tb.snapshot
Importing snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE)
Target imported table name: ydb.test_tb
Table being imported: ydb.test_tb
Successfully applied snapshot.
Object           	Old ID                           	New ID                          
Keyspace         	c478ed4f570841489dd973aacf0b3799 	c478ed4f570841489dd973aacf0b3799
Table            	ff4389ee7a9d47ff897d3cec2f18f720 	ff4389ee7a9d47ff897d3cec2f18f720
Tablet 0         	cea3aaac2f10460a880b0b4a2a4b652a 	cea3aaac2f10460a880b0b4a2a4b652a
Tablet 1         	e509cf8eedba410ba3b60c7e9138d479 	e509cf8eedba410ba3b60c7e9138d479
Snapshot         	4963ed18fc1e4f1ba38c8fcf4058b295 	4963ed18fc1e4f1ba38c8fcf4058b295
```

After importing the `metadata file` we see some changes:
 
1. `Old ID` and `New ID` for table and tablets.
2. `table_id` and `tablet_id` have chaged so we have a different paths from previously
3. Each `tablet_id` has changed, so we have different `tablet-<tablet_id>` directories.
4. When restoring we have to use the new IDs to get the right paths to move data.

Using these IDs, we can restore the previous `.snapshot` folders to the new paths. 

{{< note title="Tip" >}}
For each tablet we have to copy the snapshots folder on all replicas.
{{< /note >}}


Now we can start restoring the snapshot:
```
$ ./bin/yb-admin restore_snapshot 4963ed18fc1e4f1ba38c8fcf4058b295
Started restoring snapshot: 4963ed18fc1e4f1ba38c8fcf4058b295
```
After some time we can see that the restore has completed:
```
$ ./bin/yb-admin list_snapshots
Snapshot UUID                    	State
4963ed18fc1e4f1ba38c8fcf4058b295 	COMPLETE
```

### Step 6: Verifying the data

```
$ ./bin/cqlsh
```

```
cqlsh> SELECT * FROM ydb.test_tb;

 user_id
---------
       5

```
Finally we can delete the snapshot so we can free disk space if no longer need it.
```
$ ./bin/yb-admin delete_snapshot 4963ed18fc1e4f1ba38c8fcf4058b295
Deleted snapshot: 4963ed18fc1e4f1ba38c8fcf4058b295
```

This was a guide on how snapshoting works on Yugabyte. While some manual steps need to be followed, all of this
is automated in Yugabyte Platform and Yugabyte Cloud.

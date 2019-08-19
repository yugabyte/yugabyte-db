``---
title: Backing Up Data using Snapshot Api
linkTitle: Backing Up Data using Snapshot Api
description: Backing Up Data using Snapshot Api
image: /images/section_icons/manage/enterprise.png
headcontent: Backing up data using Snapshot Api in YugaByte DB.
menu:
  v1.2:
    identifier: manage-backup-restore-backing-up-data-snapshot
    parent: manage-backup-restore-snapshot
    weight: 703
---

This page covers backups for YugaByte DB using snapshots. Here are some points to keep in mind.

- Distributed backups using snapshots notes
  - Massively parallel, efficient for very large data sets
  - Snapshots are not transactional across the whole table but only on each tablet [#2086](https://github.com/YugaByte/yugabyte-db/issues/2086)
  - Multi table transactional snapshot is in the roadmap [#2084](https://github.com/YugaByte/yugabyte-db/issues/2084) 
  - Snapshoting is broken in YSQL [#2083](https://github.com/YugaByte/yugabyte-db/issues/2083)
  - The platform edition (enterprise) automates all this for you


In this tutorial we'll be using YCQL but the same apis are used in YSQL.

## Step 0: Create a 1=node local cluster
Read {{< ref "../../quick-start/create-local-cluster.md" >}} how to quickstart a a local cluster.

`./bin/yb-ctl create`

See {{< ref "../../admin/yb-ctl.md" >}} for yb-ctl reference

## Step 1: Creating a table with data
Read {{< ref "/api/ycql/quick-start.md" >}} for a quick start on YCQL api.

Log into cqlsh. 

```$ ./bin/cqlsh```

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

## Step 2: Creating a snapshot

Create a snapshot from `yb-admin` binary:

```
$ ./bin/yb-admin create_snapshot ydb test_tb
Started flushing table ydb.test_tb
Flush request id: fe0db953a7a5416c90f01b1e11a36d24
Waiting for flushing...
Flushing complete: SUCCESS
Started snapshot creation: 4963ed18fc1e4f1ba38c8fcf4058b295
```

The snapshot does a rocksdb flush and hardlinks the files in a snapshots folder.
We can see when it finishes by listing snapshots:

```
$ ./bin/yb-admin list_snapshots
Snapshot UUID                    	State
4963ed18fc1e4f1ba38c8fcf4058b295 	COMPLETE
```

## Step 3: Exporting the snapshot

First we need to export a meta data file that describes the snapshot. 

```
$ ./bin/yb-admin export_snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 test_tb.snapshot
Exporting snapshot 4963ed18fc1e4f1ba38c8fcf4058b295 (COMPLETE) to file test_tb.snapshot
Snapshot meta data was saved into file: test_tb.snapshot
```

Then we need to copy the actual data from the table & tablets. In this case we 
have to use a script that copies all data. The filepath structure is:

```
<yb_data_dir>/node-<node_number>/disk-<disk_number>/<api_dir>/tserver/data/rocksdb/table-<table_id>/[tablet-<tablet_id>.snapshots]/<snapshot_id>
```

* `<yb_data_dir>` is the directory where YDB data is stored. (default=`~/yugabyte-data`)
* `<node_number>` is used when multiple nodes are running on the same server (usually in testing/qa/dev) (default=1)
* `<disk_number>` when running on multiple disks (default=1)
* `<api_dir>` is `yb-data` in YCQL and `<pg-data>` in YSQL
* `<table_id>` is the UUID of the table. (can take it from WEB UI)
* `<tablet_id>` in each table we have a list of tablets. And each tablet has a tablet_id.snapshots directory, which is what we really need to copy.
* `<snapshot_id>` there is a directory for each snapshot since you can have multiple completed snapshots on each server

When snapshoting a multi-node cluster, we need to go into each node and copy 
the folders of ONLY the lead tablets on that node.


## Step 4: Deleting data and restoring a snapshot
First we delete the row from the table and verify it's no longer there.
```
cqlsh> DELETE FROM ydb.test_tb WHERE user_id=5;
cqlsh> SELECT * FROM ydb.test_tb;

 user_id
---------

(0 rows)
```

## Step 5: Copying snapshot data to another directory

First we need to get the `table_id` UUID that we're snapshoting. We can get this in 
the WEB UI [http://127.0.0.1:7000/tables](http://127.0.0.1:7000/tables) under "User Tables".

For each table, there are multiple tablets where the data is stored. We need to get a list of tablets and the Leader for each of them.

```
$ ./bin/yb-admin list_tablets ydb test_tb 0
Tablet UUID                      	Range                                                    	Leader
cea3aaac2f10460a880b0b4a2a4b652a 	partition_key_start: "" partition_key_end: "\177\377"    	127.0.0.1:9100
e509cf8eedba410ba3b60c7e9138d479 	partition_key_start: "\177\377" partition_key_end: ""    	127.0.0.1:9100
```

Using this information we can construct the full path of all directories where snapshots are stored for each tablet/snapshot_id.

We can create a small script to manually copy/move the folders to a backup directory/filesystem or external storage.


## Step 6: Triggering snapshot import
Use a script to move the `.snapshot` folders to the new cluster. Make sure they are in every replica server and not just the leaders for the tablet. 
This way each node will restore by just reading from the local filesystem
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
Snapshot         	4963ed18fc1e4f1ba38c8fcf4058b295 	49b539f60f7d493794132c2fd76e2e81
```

We get the New ID of the snapshot. Then we can use this id to restore it:

```
$ ./bin/yb-admin restore_snapshot 49b539f60f7d493794132c2fd76e2e81
Started restoring snapshot: 49b539f60f7d493794132c2fd76e2e81
```
After some time we can see that the restore has completed:
```
$ ./bin/yb-admin list_snapshots
Snapshot UUID                    	State
49b539f60f7d493794132c2fd76e2e81 	COMPLETE
```

Verifying we have the same data as previously:
```
cqlsh> SELECT * FROM ydb.test_tb;

 user_id
---------
       5

```

At last we can delete the snapshot so we can free disk space if no longer need it.

```
$ ./bin/yb-admin delete_snapshot 4963ed18fc1e4f1ba38c8fcf4058b295
Deleted snapshot: 4963ed18fc1e4f1ba38c8fcf4058b295
```


 














<ul class="nav nav-tabs nav-tabs-yb">
  <li>
    <a href="#cassandra" class="nav-link active" id="cassandra-tab" data-toggle="tab" role="tab" aria-controls="cassandra" aria-selected="true">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="cassandra" class="tab-pane fade show active" role="tabpanel" aria-labelledby="cassandra-tab">
    {{% includeMarkdown "ycql/backing-up-data.md" /%}}
  </div>
</div>





---
title: Two data center (2DC) deployment
linkTitle: Two data center (2DC) deployment
description: Two data center (2DC) deployment
menu:
  latest:
    identifier: 2dc-simulation
    parent: explore
    weight: 250
---


#### Set up producer universe

1. Create Universe `yugabyte-producer`.
2. Create YSQL table with index.

    ```postgresql
    CREATE TABLE sqlsecondaryindex(
    k text PRIMARY KEY,
    v text);
    ```

#### Set up consumer universe

1. Create Universe `yugabyte-consumer`.
2. Create the same YSQL table and index.

    ```postgresql
    CREATE TABLE sqlsecondaryindex(
    k text PRIMARY KEY,
    v text);
    ```

### Unidirectional  (one-way) replication

Setup 1-way replication on those 2 tables.
Look up producer universe UUID, and table IDs for the two tables and index table on master UI.
Run this yb-admin command
yb-admin -master_addresses <consumer_universe_master_addresses> setup_universe_replication <producer_universe_uuid> <producer_universe_master_addresses> <table_id>,[<table_id>..]

Example:
yb-admin -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a

Note that there should be 3 table IDs in the above command - two of those are YSQL for base table and index, and one for CQL table.
Please be sure to specify all master addresses for both producer and consumer universes in the above command.

### Bidirectional (two-way) replication

If you want to set up 2-way replication, then repeat the above command on “yugabyte-producer” universe. Note that this time, “yugabyte-producer” will be setup to consume data from “yugabyte-consumer”.

Load data into producer universe
Start pumping data into “yugabyte-producer” using yb-sample-apps
YSQL Example: java -jar target/yb-sample-apps.jar --workload SqlSecondaryIndex  --nodes 127.0.0.1:5433

Verify replication
Connect to “yugabyte-consumer” universe via ysqlsh and confirm that you can see expected records.


Verify 2-way replication
For 2-way replication, repeat steps (10) and (11), but pump data into “yugabyte-consumer”. To avoid primary key conflict errors, please keep the key space for the two universes separate.

Temporary (not needed from 2.0.1 release):
Because WAL retention is not getting increased correctly during CDC setup, use log_min_seconds_to_retain gflag to increase retention.

Amey Notes
1 way
bin/yb-admin -master_addresses 10.150.0.52:7100,10.150.0.58:7100,10.150.0.59:7100 setup_universe_replication 85decdeb-c56e-4071-b4d3-c6424b251eda 10.150.0.27:7100,10.150.0.38:7100,10.150.0.42:7100 75b4c27637cb43eb819fad0e6de44f61

2 way
bin/yb-admin -master_addresses 10.150.0.27:7100,10.150.0.38:7100,10.150.0.42:7100 setup_universe_replication fbd58061-dce6-46fd-b71e-ba871c067d25 10.150.0.52:7100,10.150.0.58:7100,10.150.0.59:7100 547f7b48f2e640088bb599e332550116

## Simulate 2DC on a local machine

### 1. Start the clusters

Start the first local cluster that will simulate Data Center A.

```sh
$ ./bin/yb-ctl create --data_dir $YUGABYTE_HOME/yb-datacenter-A --ip_start 1
```

This will start up a 1-node cluster using IP address of `127.0.0.1`.

Now start the second local cluster that will simulate Data Center B.

```sh
$ ./bin/yb-ctl create --data_dir $YUGABYTE_HOME/yb-datacenter-B --ip_start 2
```

This will start up a 1-node cluster using IP address of `127.0.0.2`.

### 2. Create the database tables

Create the database table on cluster A.

Open `ysqlsh` specifying the host IP address of `127.0.0.1`.

```sh
$ ./bin/ysqlsh -h 127.0.0.1
```

b. Run the following `CREATE TABLE` statement.

```postgresql
CREATE TABLE sqlsecondaryindex(
k text PRIMARY KEY,
v text);
```

Now create the identical database table on cluster B.

Open `ysqlsh` specifying the host IP address of `127.0.0.2`.

```sh
$ ./bin/ysqlsh -h 127.0.0.2
```

Run the following `CREATE TABLE` statement.

```postgresql
CREATE TABLE sqlsecondaryindex(
k text PRIMARY KEY,
v text);
```

### 3. Set up the replication

Set up cluster B to be the consumer of data from cluster A.

```postgresql
yb-admin -master_addresses 127.0.0.22:7100 setup_universe_replication cluster-A 127.0.0.11:7100      <comma-separated-list-of-table-ids-from-cluster-A>
```

[Optional] To enable bidirectional (two-way) replication, run the following `yb-admin` `setup_universe_replication` command to set up cluster A to be the consumer of cluster B.

```postgresql
yb-admin -master_addresses 127.0.0.11:7100 setup_universe_replication datacenter-B 127.0.0.22:7100      <comma-separated-list-of-table-ids-from-cluster-B>
```

Now, use `ysqlsh` to load data on any or both of those clusters and see the data appear on the other cluster.

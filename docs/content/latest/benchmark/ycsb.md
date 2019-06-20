---
title: YCSB
linkTitle: YCSB
description: YCSB
image: /images/section_icons/architecture/concepts.png
headcontent: Benchmark YugaByte DB using YCSB.
menu:
  latest:
    identifier: benchmark-ycsb
    parent: benchmark
    weight: 740
aliases:
  - /benchmark/ycsb/
showAsideToc: True
isTocNested: True
---

{{< note title="Note" >}}
For more information about YCSB see: 

* YCSB Wiki: https://github.com/brianfrankcooper/YCSB/wiki
* Workload info: https://github.com/brianfrankcooper/YCSB/wiki/Core-Workloads
{{< /note >}}


We will first setup YCSB and configure it to use the YCQL Driver for Cassandra.

## Step 1.Clone the YCSB repository

You can do this by running the following commands.

```sh
cd $HOME
git clone https://github.com/brianfrankcooper/YCSB.git
cd YCSB
```

## Step 2. Use YCQL Driver

1. In pom.xml change the line
```
<cassandra.cql.version>3.0.0</cassandra.cql.version>
```
to the latest version of the YugaByte-Cassandra driver
```
<cassandra.cql.version>3.2.0-yb-17</cassandra.cql.version>
```

{{< note title="Note" >}}
You can (and probably should) always check Maven to find the latest version.
{{< /note >}}


2. In cassandra/pom.xml change the line
```
<groupId>com.datastax.cassandra</groupId>
```
to
``` 
<groupId>com.yugabyte</groupId>
```

## Step 3. Build YCSB

You can build YCSB by running the following command:

```sh
mvn -pl com.yahoo.ycsb:cassandra-binding -am clean package -DskipTests
```

## Step 4. Setup cqlsh

You can setup YugaByte-cqlsh by doing the following:

```sh
cd $HOME
git clone https://github.com/YugaByte/cqlsh
```

## Step 5. Prepare the driver script

1. Create an executable file in the YCSB folder:

```sh
cd $HOME/YCSB
touch run-yb-ycsb.sh
chmod a+x run-yb-ycsb.sh
```

2. Copy the following contents in `run-yb-ycsb.sh`:

{{< note title="Note" >}}
You may want to customize the values below (such as <ip-addr>) with the correct/intended ones for your setup.
{{< /note >}}

```sh
#!/bin/bash


# YB-CQL host (any of the yb-tserver hosts)
# (The other nodes should get automatically discovered by the driver).
hosts=127.0.0.1
ycsb=$HOME/YCSB/bin/ycsb
cqlsh=$HOME/cqlsh/bin/cqlsh <ip-addr>
ycsb_setup_script=$HOME/YCSB/cassandra/src/test/resources/ycsb.cql
keyspace=ycsb
table=usertable
# See https://github.com/brianfrankcooper/YCSB/wiki/Core-Properties for param descriptions
params="-p recordcount=1000000 -p operationcount=10000000"

setup() {
    $cqlsh <<EOF
create keyspace $keyspace with replication = {'class': 'SimpleStrategy', 'replication_factor' : 3};
EOF
    $cqlsh -k $keyspace -f $ycsb_setup_script
}

cleanup() {
    $cqlsh <<EOF
drop table $keyspace.$table;
drop keyspace $keyspace;
EOF
}

delete_data() {
    $cqlsh -k $keyspace <<EOF
drop table $table;
EOF
    $cqlsh -k $keyspace -f $ycsb_setup_script
}


run_workload() {
    local workload=$1-----------------
    echo =========================== $workload ===========================
    $ycsb load cassandra-cql -p hosts=$hosts -P workloads/$workload $params \
      -p threadcount=40 | tee $workload-load.dat
    $ycsb run cassandra-cql -p hosts=$hosts -P workloads/$workload $params \
      -p cassandra.readconsistencylevel=QUORUM -p cassandra.writeconsistencylevel=QUORUM \
      -p threadcount=256 -p maxexecutiontime=180 | tee $workload-transaction.dat
    delete_data
}

setup

load_data workloada
run_workload workloada
run_workload workloadb
run_workload workloadc
run_workload workloadf
run_workload workloadd

truncate_table
load_data workloade
run_workload workloade

cleanup
```

We use YugaByte DB with strongly consistent reads and writes, which corresponds, in Cassandra, to using the `QUORUM` option for both `cassandra.readconsistencylevel` and `cassandra.writeconsistencylevel` (see the command above).

## Step 6. Run and Check Results

Simply run the script above:
```sh
./run-yb-ycsb.sh
```

{{< tip title="Tip" >}}
See also [this page](https://github.com/brianfrankcooper/YCSB/wiki/Running-a-Workload-in-Parallel) about running workloads in parallel.
{{< /tip >}}

Results for each workload will be in `workload[abcdef]-transaction.dat` (e.g. `workloada-transaction.dat`).

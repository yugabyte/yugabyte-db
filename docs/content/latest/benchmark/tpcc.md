---
title: TPCC
linkTitle: TPCC
description: TPCC
image: /images/section_icons/architecture/concepts.png
headcontent: Benchmark YugabyteDB using TPCC.
menu:
  latest:
    identifier: benchmark-tpcc
    parent: benchmark
    weight: 740
aliases:
  - /benchmark/tpcc/
showAsideToc: True
isTocNested: True
---

Following are the steps to be taken to run TPCC against Yugabyte.

## Step 1. Download the TPCC binaries.

You can do this by running the following commands.

```sh
cd $HOME
wget https://github.com/yugabyte/tpcc/releases/download/1.0/tpcc.tar.gz
tar -zxvf tpcc.tar.gz
cd tpcc
```

## Step 2. Start your database
Start the database using steps mentioned here: https://docs.yugabyte.com/latest/quick-start/explore-ysql/.

## Step 3. Configure connection properties
Set the following connection configurations in the workload config file.

```sh
<!-- Connection details -->
<dbtype>postgres</dbtype>
<driver>org.postgresql.Driver</driver>
<DBUrl>jdbc:postgresql://<ip>:5433/yugabyte</DBUrl>
<username>yugabyte</username>
<password></password>
<isolation>TRANSACTION_REPEATABLE_READ</isolation>
```

The details of the workloads have already been populated in the sample config present in /config.
The workload descriptor works the same way as it does in the upstream branch and details can be found in the [on-line documentation](https://github.com/oltpbenchmark/oltpbench/wiki).

## Step 4. Running the Benchmark
A utility script (./tpccbenchmark) is provided for running the benchmark. The options are

```
-c,--config &lt;arg&gt;            [required] Workload configuration file
   --clear &lt;arg&gt;             Clear all records in the database for this
                             benchmark
   --create &lt;arg&gt;            Initialize the database for this benchmark
   --dialects-export &lt;arg&gt;   Export benchmark SQL to a dialects file
   --execute &lt;arg&gt;           Execute the benchmark workload
-h,--help                    Print this help
   --histograms              Print txn histograms
   --load &lt;arg&gt;              Load data using the benchmark's data loader
-o,--output &lt;arg&gt;            Output file (default System.out)
   --runscript &lt;arg&gt;         Run an SQL script
-s,--sample &lt;arg&gt;            Sampling window
-v,--verbose                 Display Messages
```

Before you can actually run the workload, you need to "load" the data first.

```sh
./tpccbenchmark -c config/workload_1.xml --create=true --load=true
```

Then, you can run the workload:

```sh
./tpccbenchmark -c config/workload_1.xml --execute=true -s 5 -o outputfile
```

We can also load and perform the benchmark in a single step:

```sh
./tpccbenchmark -c config/workload_1.xml --create=true --load=true --execute=true -s 5 -o outputfile
```

The config directory has a few configurations for the various workloads. We can run any of those workloads by changing the configuration file.

```sh
./tpccbenchmark -c config/workload_2.xml --create=true --load=true --execute=true -s 5 -o outputfile
```

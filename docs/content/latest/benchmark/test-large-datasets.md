---
title: Test Large Datasets
linkTitle: Test Large Datasets
description: Test Large Datasets
image: /images/section_icons/architecture/concepts.png
headcontent: Testing YugaByte DB with large data sets.
menu:
  latest:
    identifier: benchmark-test-large-datasets
    parent: benchmark
    weight: 740
aliases:
  - /benchmark/test-large-datasets/
showAsideToc: True
isTocNested: True
---

## Goal

The goal of this benchmark is to understand the performance, failure and scaling characterics of YugaByte DB with a massive dataset (multiple TB per node). In order to accomplish that, we will do the following:

* Load 30 billion key-value records
* Each write operation inserts a single record
* Perform a read-heavy workload that does *random reads* in the presence of some writes
* Perform a read-heavy workload that does *reads of a subset of data* in the presence of some writes

Each record is a key-value record of size almost 300 bytes.

* Key size: 50 Bytes
* Value size: 256 Bytes (chosen to be not very compressible)

## Recommended Configuration

Note that the load tester was run from a separate machine in the same AZ.

### Machine Types

A machine in the AWS cloud with the following spec was chosen: **32-vcpus, 240 GB RAM, 4 x 1.9TB nvme SSD**.

* Cloud: **AWS**
* Node Type: **i3.8xlarge**

### Cluster Creation

Create a standard 4 node cluster, with replication factor of 3. Pass the following option to the YugaByte DB processes.

```
--yb_num_shards_per_tserver=20
```

The `yb_num_shards_per_tserver` was set to **20** (from the default value of 8). This is done because the i3.8xlarge nodes have 4 disks. In future, YugaByte DB will automatically pick better defaults for nodes with multiple disks.

Export the following environment variable:
```
$ export YCQL_ADDRS="<ip1>:9042,<ip2>:9042,<ip3>:9042,<ip4>:9042"
```

## Initial Load Phase

The data was loaded at a steady rate over about 4 days using the CassandraKeyValue sample application. The command to load the data is shown below:


```sh
$ java -jar yb-sample-apps.jar        \
      --workload CassandraKeyValue    \
      --nouuid --nodes $YCQL_ADDRS    \
      --value_size 256                \
      --num_unique_keys 30000000000   \
      --num_writes 30000000000        \
      --num_threads_write 256         \
      --num_threads_read 1
```

### Write IOPS

You should see a steady 85K inserts/sec with the write latencies in the 2.5ms ballpark. This is shown graphically below.

![Load Phase Results](/images/benchmark/bench-large-dataset-inserts-1.png)

### Data set size growth rate

The graph below shows the steady growth in SSTables size at a node from Sep 4 to Sep 7th beyond which it stabilizes at 6.5TB.

![Load Phase Results](/images/benchmark/bench-large-dataset-inserts-2.png)


## Final data set size

The figure below is from the yb-master Admin UI shows the tablet servers, number of tablets on each, number of tablet leaders and size of the on-disk SSTable files.

{{< note title="Note" >}}
The uncompressed dataset size per node is 8TB, while the compressed size is 6.5TB. This is because the load generator generates random bytes, which are not very compressible.

Real world workloads generally have much more compressible data.
{{< /note >}}


![Load Phase Results](/images/benchmark/bench-large-dataset-inserts-3.png)



## Expected Results

The results you see should be in the same ballpark as shown below.

### Load Phase Results

Name    | Observation
--------|------
Records inserted   | 30 Billion
Size of each record | ~ 300 bytes
Time taken to insert data | 4.4 days
Sustained insert Rate        | 85K inserts/second
Final dataset in cluster  | 26TB across 4 nodes
Final dataset size per node     | 6.5TB / node

### Read-Heavy Workload Results

Name    | Observation
--------|------
Random-data read heavy workload | 185K reads/sec and 1K writes/sec
Recent-data read heavy Workload | 385K reads/sec and 6.5K writes/sec


### Cluster Expansion and Induced Failures

* Expanded from 4 to 5 nodes in about 8 hours
  * Deliberately rate limited at 200MB/sec
* New node takes traffic as soon the first tablet arrives
  * Pressure relieved from old nodes very quickly
* Induced one node failure in 5 node cluster
  * Cluster rebalanced in 2 hrs 10 minutes

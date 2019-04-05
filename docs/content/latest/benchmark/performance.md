---
title: Performance
linkTitle: Performance
description: Performance
image: /images/section_icons/architecture/concepts.png
headcontent: Test the performance of YugaByte DB for various workloads.
menu:
  latest:
    identifier: benchmark-performance
    parent: benchmark
    weight: 720
aliases:
  - /benchmark/performance/
showAsideToc: True
isTocNested: True
---

## Goal 

The goal of this benchmark is to get an idea of the performance of YugaByte DB using a key-value workload.

## Recommended Configuration

### Cluster Configuration

For this benchmark, we will setup a 3-node cluster with a replication factor of 3.

### Machine Configuration

* Amazon Web Services 

  * Instance type: i3.4xlarge
  * Disks: 2 x 1.9 TB NVMe SSDs (comes pre-configured with the instance)

* Google Cloud Platform

  * Instance type: n1-standard-16
  * Disks: 2 x 375 GB SSDs


* On-Premises Datacenter

  * Instance: 16 CPU cores
  * Disk size: 1 x 200 GB SSD (minimum)
  * RAM size: 30 GB (minimum)


### Benchmark Tool

We will use the `yb-sample-apps.jar` tool to perform this benchmark. You can get it from [this GitHub repository](https://github.com/YugaByte/yb-sample-apps) as shown below.

```sh
$ wget https://github.com/YugaByte/yb-sample-apps/releases/download/v1.2.0/yb-sample-apps.jar?raw=true -O yb-sample-apps.jar 
```

You would need to install java in order to run this tool. Also export the environment variable  $ENDPOINTS containing the IP addresses (plus port) for the nodes of the cluster.

```
ENDPOINTS="X.X.X.X:9042,X.X.X.X:9042,X.X.X.X:9042"
```


## Write-heavy KV workload 

Run the key-value workload with higher number of write threads (representing write-heavy workload).  

Load 1B keys of 256 bytes each across 256 writer threads

```sh
$ java -jar ./yb-sample-apps.jar  \
      --workload CassandraKeyValue   \
      --nodes $ENDPOINTS             \
      --nouuid                       \
      --value_size 256               \
      --num_threads_read 0           \
      --num_threads_write  256       \
      --num_unique_keys 1000000000
```

### Expected Results

Name    | Observation
--------|------
Write Ops/sec | ~90k
Read Latency | ~2.5/3.0 ms/op
CPU (User + Sys) | 60%


## Read-heavy KV workload 

Run the key-value workload with higher number of read threads (representing read-heavy workload). 

Load 1M keys of 256 bytes and access them with 256 reader threads

```sh
$ java -jar ./yb-sample-apps.jar  \
      --workload CassandraKeyValue   \
      --nodes $ENDPOINTS             \
      --nouuid                       \
      --value_size 256               \
      --num_threads_read 256         \
      --num_threads_write 0          \
      --num_unique_keys 1000000
```

### Expected Results


Name    | Observation
--------|------
(Read) Ops/sec | ~150k
(Read) Latency | ~1.66 ms/op
CPU (User + Sys) | 60%


## Batch Write-heavy KV workload 

Run the key-value workload in batch mode and higher number of write threads (representing batched, write-heavy workload).  

Load 1B keys of 256 bytes each across 64 writer threads in batches of 25 each

```sh
$ java -jar ./yb-sample-apps.jar      \
      --workload CassandraBatchKeyValue  \
      --nodes $ENDPOINTS                 \
      --nouuid                           \
      --batch_size 25                    \
      --value_size 256                   \
      --num_threads_read 0               \
      --num_threads_write 64             \
      --num_unique_keys 1000000000
```

### Expected Results

Name    | Observation
--------|------
(Batch Write) Ops/sec | ~140k
(Batch Write) Latency | ~9.0 ms/op
CPU (User + Sys) | 80%

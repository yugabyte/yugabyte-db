---
title: Perfomance
linkTitle: Perfomance
description: Perfomance
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: performance
    parent: yb-voyager
    weight: 103
isTocNested: true
showAsideToc: true
---


This page explains the factors which may affect the performance of migration jobs being carried out using [yb-voyager](https://github.com/yugabyte/yb-voyager). It also explains the tunable parameters to improve the performance of jobs.

## Factors affecting export performance

- How busy is the source database

- Size of the tables

- Storage Type

- Parallel export of data from multiple tables

## Factors affecting import data performance

Some of the factors known to slow down the performance of data ingestion in any database are as follows:

- Secondary indexes - Presence of secondary indexes slows down the insert speeds. As the number of indexes increase, the insert speed decreases, because the index structures get updated for every insert.

- Constraint checks  - Every insert has to satisfy constraints (including foreign key constraints, value constraints and so on) if defined on the table which results in extra processing. In distributed databases, this becomes pronounced as foreign key checks may invariably mean talking to peer servers.

- Trigger actions - If triggers are defined on tables for every insert, then the corresponding trigger action(for each insert) is executed, which slows down ingestion.

However, we can take choose techniques to reduce the impact on the performance when we are migrating data for the first time into a newly created empty database. For example, yb-voyager assumes that it should be perfectly fine to:

- Create secondary indexes after the data import is complete - YugabyteDB has a feature called [Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md). This feature allows updating indexes after data has been loaded to the main table. This is a lot faster than online index maintenance. The [import data](../../yb-voyager/migrate-steps/#import-data) phase in yb-voyager creates the secondary indexes after it completes data loading on all the tables.

- Disable constraint checks - It is safe to disable all the constraint checks, provided the data is from a reliable source. For maximum throughput, it is also preferred to not follow any order in which tables are populated. yb-voyager therefore, disables all the constraints, except primary key violation errors during the data import phase.

{{< note title="Note" >}}

yb-voyager disables all the constraints only in internal sessions to migrate data.

{{< /note >}}

- Disable Triggers - Triggers are disabled in those sessions where yb-voyager uses to import data in a YugabyteDB cluster, because it results in repeating trigger actions in the new target database which is not required.

## Other Factors

There are other factors which can impact the speed of data ingestion. One or more or all of the following can be done to increase the ingestion performance.

- Data load parallelism - The yb-voyager engine executes ‘N’ parallel batch ingestion jobs at any given time where ‘N’ is equal to the number of nodes in the YugabyteDB cluster. Normally this is considered as a good default practice. However, the target YugabyteDB cluster can be running on high resource machines. Machines which have a large number of cpu cores. In such cases, the default parallelism may seem unfit resulting in underutilization of cpu resources.

It is good practice to have at least ‘C’ number of batch ingestion jobs run simultaneously, where ‘C’ is equal to the sum total of all the cores available in the entire cluster.

{{< note title="Note" >}}

If the cpu usage is going beyond 80%, then it is recommended to lower the parallelism a bit. Similarly if the cpu% is still low, one can increase the parallelism. Include the [–parallel-jobs](../../yb-voyager/yb-voyager-cli/#parallel-jobs) to the import data command to override the default parallelism by specifying the number of connections.

{{< /note >}}

- Batch size - Choosing a very small value of [--batch-size](../../yb-voyager/yb-voyager-cli/#batch-size) (Default is 100000) might result in slow performance, because the time spent on actual data import might be comparable/less to time spent on other tasks such as bookkeeping, setting up the client connection and so on.

- Disk write contention - YugabyteDB servers can be passed with one or multiple disk volumes to store tablet data. If all the tablets are writing to a single disk, then write contention can slow down the ingestion speed. In such cases, configuring the [yb-tservers](../../../reference/configuration/yb-tserver/) with multiple disks helps in reducing the disk write contention; thereby increasing the throughput. Additionally, disks with higher IOPS and better throughput improve the write performance.

- Number of table splits:

  - For larger tables and indexes that are [hash](../../../architecture/docdb-sharding/sharding/#hash-sharding) sharded, specify the number of initial tablet splits desired as a part of the DDL statement of the table. This can be very beneficial to distribute the data of the table across multiple nodes right from the get go. Refer to [hash-sharded tables](../../../architecture/docdb-sharding/tablet-splitting/#hash-sharded-tables) for an example to specify the number of tablets at table creation time.

  - For larger tables and indexes that are [range](../../../architecture/docdb-sharding/sharding/#range-sharding) sharded, and the value ranges of the primary key columns are known ahead of time, pre-split them at the time of creation. Refer to [range-sharded tables](../../../architecture/docdb-sharding/tablet-splitting/#hash-sharded-tables) for an example to specify the split points.

- Larger cluster - If the cluster size is increased, then the write contention is reduced and it helps with faster data ingestion.

{{< note title="Note" >}}

All the performance optimizations suggested for data import phase is applicable for ingestion from _files_ as well.

{{< /note >}}

## Experiment showcasing how to obtain good ingestion speed

With increased disk and CPU usage, the data ingestion speed is faster.
The following table includes details from an experiment for importing from a CSV file.

| Experiment | Cluster configuration | yb-voyager flags | CPU usage | Average throughput |
| :---------- | :------------------- | :--------------- | :-------- | :----------------- |
| 3 parallel jobs | 3 node [RF](../../../architecture/docdb-replication/replication/#replication-factor) 3 cluster, c5.4x large ( 16 cores 32gb ) <br> 1 EBS Type gp3 disk per node, 3000 IOPS, 125 MiB bandwidth | batch-size=200k, parallel-jobs=3 | CPU was observed to hover around 50% throughout the ingestion period | 43K rows/sec |
| Same machine with increased parallelism ( 1 per core ) | 3 node [RF](../../../architecture/docdb-replication/replication/#replication-factor) cluster, c5.4x large ( 16 cores 32gb ) <br> 1 EBS Type gp3 disk per node, 3000 IOPS, 125 MiB bandwidth | batch-size=200k, parallel-jobs=48 | CPU was observed to hover around 80% throughout the ingestion period | 117K rows/sec | -->
| Same machine with multiple disks per yb-tserver | 3 node [RF](../../../architecture/docdb-replication/replication/#replication-factor) cluster, c5.4x large ( 16 cores 32gb ) <br> 4 EBS Type gp3 disk per node, 3000 IOPS, 125 MiB bandwidth | | CPU was observed to hover around 90% | 193K rows/sec |
| With a larger cluster | 6 Node [RF](../../../architecture/docdb-replication/replication/#replication-factor) cluster, c5.4x large ( 16 cores 32gb ) <br> 4 EBS Type gp3 disk per node, 3000 IOPS, 125 MiB bandwidth | | CPU was observed to hover around 90% | 227K rows/sec |

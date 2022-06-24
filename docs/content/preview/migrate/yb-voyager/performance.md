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


This page describes factors that can affect the performance of migration jobs being carried out using [yb-voyager](https://github.com/yugabyte/yb-voyager). It also explains the tuneable parameters you can use to improve performance.

## Improve import performance

Some of the factors known to slow down the performance of data ingestion in any database are as follows:

- Secondary indexes - secondary indexes slow down insert speeds. As the number of indexes increases, insert speed decreases, because the index structures need to be updated for every insert.

- Constraint checks - Every insert has to satisfy constraints (including foreign key constraints, value constraints, and so on) if defined on the table, which results in extra processing. In distributed databases, this becomes pronounced as foreign key checks may invariably mean talking to peer servers.

- Trigger actions - If triggers are defined on tables for every insert, then the corresponding trigger action (for each insert) is executed, which slows down ingestion.

To maximize performance when migrating data into a newly created empty database, yb-voyager does the following:

- Creates secondary indexes after the data import is complete. During the [import data](../../yb-voyager/migrate-steps/#import-data) phase, yb-voyager creates the secondary indexes after it completes data loading on all the tables. Then use [Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) to update indexes after data is loaded. This is much faster than online index maintenance.

- Disables constraint checks - It is safe to disable all the constraint checks, provided the data is from a reliable source. For maximum throughput, it is also preferable to not follow any order when populating tables. During the data import phase, yb-voyager disables all constraints, except for primary key violation errors.

{{< note title="Note" >}}

yb-voyager disables all the constraints only in internal sessions to migrate data.

{{< /note >}}

- Disables triggers. yb-voyager disables triggers during import to avoiding unnecessary repeating trigger actions in the new target database.

## Other Factors

Use one or more of the following techniques to improve migration performance:

- Data load parallelism - yb-voyager executes ‘N’ parallel batch ingestion jobs at any given time, where ‘N’ is equal to the number of nodes in the YugabyteDB cluster. Normally this is considered a good default practice. However, if the target YugabyteDB cluster is running on high resource machines with a large number of cpu cores, the default may result in underutilizing CPU resources.

You should have at least ‘C’ number of batch ingestion jobs run simultaneously, where ‘C’ is equal to the sum total of all the cores available in the entire cluster.

{{< note title="Note" >}}

If CPU use is greater than 80%, you should lower the parallelism. Similarly, if CPU use is low, you can increase the parallelism. Use the [–parallel-jobs](../../yb-voyager/yb-voyager-cli/#parallel-jobs) argument with the import data command to override the default parallelism by specifying the number of connections.

{{< /note >}}

- Batch size - Choosing a very small value for [--batch-size](../../yb-voyager/yb-voyager-cli/#batch-size) (Default is 100000) can result in slow performance, because the time spent on actual data import might be comparable or less than time spent on other tasks such as bookkeeping, setting up the client connection, and so on.

- Disk write contention - YugabyteDB servers can be configured with one or multiple disk volumes to store tablet data. If all tablets are writing to a single disk, write contention can slow down the ingestion speed. Configuring the [YB-TServers](../../../reference/configuration/yb-tserver/) with multiple disks can reduce disk write contention, thereby increasing throughput. Disks with higher IOPS and better throughput also improve write performance.

- Number of table splits:

  - For larger tables and indexes that are [hash](../../../architecture/docdb-sharding/sharding/#hash-sharding) sharded, specify the number of initial tablet splits as a part of the table DDL statement. This can help distribute the table data across multiple nodes right from the get go. Refer to [hash-sharded tables](../../../architecture/docdb-sharding/tablet-splitting/#hash-sharded-tables) for an example of how to specify the number of tablets at table creation time.

  - For larger tables and indexes that are [range](../../../architecture/docdb-sharding/sharding/#range-sharding) sharded, if the value ranges of the primary key columns are known ahead of time, pre-split them at the time of creation. Refer to [range-sharded tables](../../../architecture/docdb-sharding/tablet-splitting/#hash-sharded-tables) for an example of how to specify the split points.

- Increase cluster size - Write contention is reduced with larger cluster sizes.

{{< note title="Note" >}}

These performance optimizations apply to both the data import phase and for importing from _files_.

{{< /note >}}

## Factors affecting export performance

- Parallel export of data from multiple tables - yb-voyager by default exports one table data at a time. Include the [–parallel-jobs](../../yb-voyager/yb-voyager-cli/#parallel-jobs) to the export data command to override the default of 1. Increasing them to a big value may have a negative impact as well. We have noticed that a value of '4' is good. 

## Data import speeds

- Experiment details : Import from a CSV file

- File size : 40GB

- Number of rows : 120 million

- Table schema:

```sql
CREATE TABLE topology_flat (
    userid_fill uuid,
    idtype_fill text,
    userid uuid,
    idtype text,
    level int,
    locationgroupid uuid,
    locationid uuid,
    parentid uuid,
    attrs jsonb,
    PRIMARY KEY (userid, level, locationgroupid, parentid, locationid)
);
```

With increased disk and CPU usage, the data ingestion speed is faster. The experiment includes runs performed with varying configurations including more parallel jobs, multiple disks, and a larger cluster.

The following table includes results from the experiment. Notice the increasing average throughput numbers as more optimizations are introduced with each run.

| Experiment | Cluster configuration | yb-voyager flags | CPU usage | Average throughput |
| :---------- | :------------------- | :--------------- | :-------- | :----------------- |
| 3 parallel jobs | 3 node [RF](../../../architecture/docdb-replication/replication/#replication-factor) 3 cluster, c5.4x large (16 cores 32 GB) <br> 1 EBS Type gp3 disk per node, 3000 IOPS, 125 MiB bandwidth | batch-size=200k, parallel-jobs=3 | ~50% | 43K rows/sec |
| Increased parallelism (1 per core ) | 3 node RF 3 cluster, c5.4x large (16 cores 32 GB) <br> 1 EBS Type gp3 disk per node, 3000 IOPS, 125 MiB bandwidth | batch-size=200k, parallel-jobs=48 | ~80% | 117K rows/sec | -->
| Multiple disks per YB-TServer | 3 node RF 3 cluster, c5.4x large (16 cores 32GB) <br> 4 EBS Type gp3 disks per node, 3000 IOPS, 125 MiB bandwidth | ~90% | 193K rows/sec |
| Additional nodes | 6 Node RF 3 cluster, c5.4x large (16 cores 32GB) <br> 4 EBS Type gp3 disks per node, 3000 IOPS, 125 MiB bandwidth | ~90% | 227K rows/sec |

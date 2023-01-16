---
title: Performance
linkTitle: Performance
description: Performance
menu:
  preview:
    identifier: performance
    parent: voyager
    weight: 103
type: docs
---

This page describes factors that can affect the performance of migration jobs being carried out using [yb-voyager](https://github.com/yugabyte/yb-voyager), along with the tuneable parameters you can use to improve performance.

## Improve import performance

There are several factors that slow down data-ingestion performance in any database:

- **Secondary indexes** slow down insert speeds. As the number of indexes increases, insert speed decreases, because the index structures need to be updated for every insert.

- **Constraint checks**. Every insert has to satisfy the constraints (including foreign key constraints, value constraints, and so on) defined on a table, which results in extra processing. In distributed databases, this becomes pronounced as foreign key checks invariably mean talking to peer servers.

- **Trigger actions**. If triggers are defined on tables for every insert, then the corresponding trigger action (for each insert) is executed, which slows down ingestion.

yb-voyager maximizes performance when migrating data into a newly created empty database in several ways:

- Creates secondary indexes after the data import is complete. During the [post-import data](../migrate-steps/#import-indexes-and-triggers) phase, yb-voyager creates the secondary indexes (except the unique indexes) after it completes data loading on all the tables. Then, it uses [Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) to update indexes after data is loaded. This is much faster than online index maintenance. (Unique indexes are created in the [import-schema](../migrate-steps/#import-schema) phase, to avoid any foreign key dependencies on the index.)

- Disables constraint checks except check constraints on the column. It's safe to disable all the constraint checks provided the data is from a reliable source. For maximum throughput, it is also preferable to not follow any order when populating tables. During the data import phase, yb-voyager disables all constraints, except for primary key violation errors.

  {{< note title="Note" >}}

yb-voyager only disables all the constraints except check constraints on the column in internal sessions to migrate data.

  {{< /note >}}

- Disables triggers during import in the target database.

### Techniques to improve performance

Use one or more of the following techniques to improve import performance:

- **Load data in parallel**. yb-voyager executes N parallel batch ingestion jobs at any given time, where N is equal to half of the total number of cores in the YugabyteDB cluster. Normally this is considered good default practice. However, if the target cluster runs on high resource machines with a large number of CPU cores, the default may result in underusing CPU resources.

  Use the [-–parallel-jobs](../yb-voyager-cli/#parallel-jobs) argument with the import data command to override the default setting. Set `--parallel-jobs` to a number higher than the default based on your cluster requirement.

  If CPU use is greater than 50-60%, you should lower the number of jobs. Similarly, if CPU use is low, you can increase the number of jobs.

   {{< note title="Note" >}}

   If timeouts are witnessed, then import should be restarted with less parallelism.

   {{< /note >}}

- **Increase batch size**. If the [--batch-size](../yb-voyager-cli/#batch-size) (default is 20000) is too small, the import will run slower because the time spent importing data may be comparable or less than the time spent on other tasks, such as bookkeeping, setting up the client connection, and so on. Increasing the batch-size to a very high value is not recommended as the whole batch is executed in one transaction.

- **Add disks** to reduce disk write contention. YugabyteDB servers can be configured with one or multiple disk volumes to store tablet data. If all tablets are writing to a single disk, write contention can slow down the ingestion speed. Configuring the [YB-TServers](../../reference/configuration/yb-tserver/) with multiple disks can reduce disk write contention, thereby increasing throughput. Disks with higher IOPS and better throughput also improve write performance.

- **Enabling packed columns** helps in increasing the throughput to more than two times. Enable packed columns on the YugabyteDB cluster by setting the YB-TServer gflag `ysql_enable_packed_row` to true.

- **Client machine's disk** with higher IOPS and better throughput also improves the import performance as the performance of splitter which splits the large data file into smaller splits of 20000 rows depends on the client machine's disk.

{{< note title="Note" >}}

These performance optimizations apply whether you are importing data using the yb-voyager [import data command](../migrate-steps/#import-data) or the [import data file command](../migrate-steps/#import-data-file).

{{< /note >}}

## Improve export performance

By default, yb-voyager exports one table at a time. To improve data export, parallelize the export of data from multiple tables using the [–-parallel-jobs](../yb-voyager-cli/#parallel-jobs) argument with the export data command to increase the number of jobs. Setting the value too high can however negatively impact performance; a setting of '4' typically performs well.

## Test results

yb-voyager was tested using varying configurations, including more parallel jobs, multiple disks, and a larger cluster. The tests were run using a 28GB CSV file with 350 million rows on YugabyteDB version 2.16.0.0-b90.

The table schema for the test was as follows:

```sql
CREATE TABLE public.accounts (
    block bigint NOT NULL,discr
    address text NOT NULL,
    dc_balance bigint DEFAULT 0 NOT NULL,
    dc_nonce bigint DEFAULT 0 NOT NULL,
    security_balance bigint DEFAULT 0 NOT NULL,
    security_nonce bigint DEFAULT 0 NOT NULL,
    balance bigint DEFAULT 0 NOT NULL,
    nonce bigint DEFAULT 0 NOT NULL,
    staked_balance bigint,
    PRIMARY KEY (block, address)
);
```

As more optimizations are introduced, average throughput increases. The following table shows the results.

| Run | Cluster configuration | yb-voyager flags | CPU usage | Average throughput |
| :-- | :-------------------- | :--------------- | :-------- | :----------------- |
| 24 parallel jobs (default) | 3 node [RF](../../architecture/docdb-replication/replication/#replication-factor) 3 cluster, c5.4x large (16 cores 32 GB) <br> 1 EBS Type gp3 disk per node, 10000 IOPS, 500 MiB bandwidth | batch-size=20k<br>parallel-jobs=24 | ~80% | 44014 rows/sec |
| Increase jobs<br>(1 per core) | 3 node RF 3 cluster, c5.4x large (16 cores 32 GB) <br> 1 EBS Type gp3 disk per node, 10000 IOPS, 500 MiB bandwidth | batch-size=20k<br>parallel-jobs=48 | ~95% | 47696 rows/sec |
| Add nodes | 6 Node RF 3 cluster, c5.4x large (16 cores 32GB) <br> 4 EBS Type gp3 disks per node, 10000 IOPS, 500 MiB bandwidth | batch-size=20k<br>parallel-jobs=48 | ~80% | 86547 rows/sec |
| Enabling packed columns | 3 node RF 3 cluster, c5.4x large (16 cores 32 GB) <br> 1 EBS Type gp3 disk per node, 10000 IOPS, 500 MiB bandwidth | batch-size=20k<br>parallel-jobs=48<br>YB-TServer gflag: `ysql_enable_packed_row` = `true` | ~95% | 134048 rows/sec |

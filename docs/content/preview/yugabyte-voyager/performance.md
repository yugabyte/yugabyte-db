---
title: Tune performance
linkTitle: Tune performance
description: Performance
menu:
  preview_yugabyte-voyager:
    identifier: performance
    parent: yugabytedb-voyager
    weight: 103
type: docs
---

This page describes factors that can affect the performance of migration jobs being carried out using [yb-voyager](https://github.com/yugabyte/yb-voyager), along with the tuneable parameters you can use to improve performance.

## Improve import performance

There are several factors that slow down data-ingestion performance in any database:

- **Secondary indexes** slow down insert speeds. As the number of indexes increases, insert speed decreases, because the index structures need to be updated for every insert.

- **Constraint checks**. Every insert has to satisfy the constraints (including foreign key constraints, value constraints, and so on) defined on a table, which results in extra processing. In distributed databases, this becomes pronounced as foreign key checks invariably mean talking to peer servers.

- **Trigger actions**. If triggers are defined on tables for every insert, then the corresponding trigger action (for each insert) is executed, which slows down ingestion.

yb-voyager improves performance when migrating data into a newly created empty database in several ways:

- Creates secondary indexes after the data import is complete. During the [post-import data](../migrate/migrate-steps/#import-indexes-and-triggers) phase, yb-voyager creates the secondary indexes (except the unique indexes) after it completes data loading on all the tables. Then, it uses [Index Backfill](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/online-index-backfill.md) to update indexes after data is loaded. This is much faster than online index maintenance. (Unique indexes are created in the [import-schema](../migrate/migrate-steps/#import-schema) phase, to avoid any issues during import of schema because of foreign key dependencies on the index.)

- Disables foreign key constraints during data import. However, other constraints like primary key constraints, check constraints, unique key constraints, and so on are not disabled. It's safe to disable some constraint checks as the data is from a reliable source. For maximum throughput, it is also preferable to not follow any order when populating tables.

- Disables triggers also during the import data phase.

  {{< note title="Note" >}}

yb-voyager only disables the constraint checks and triggers in the internal sessions it uses to migrate data.

  {{< /note >}}

### Techniques to improve performance

Use one or more of the following techniques to improve import data performance:

- **Load data in parallel**. yb-voyager executes N parallel batch ingestion jobs at any given time, where N is equal to half of the total number of cores in the YugabyteDB cluster. Normally this is a good default value and should consume around 50-60% of CPU usage. However, if the target cluster shows high CPU utilisation, then you should lower the number of parallel jobs. Similarly, if the target cluster seems to be under-utilised, then it is recommended to stop the import, and restart it with a higher number of parallel jobs.

  Use the [-–parallel-jobs](../reference/data-migration/import-data/#arguments) argument with the import data command to override the default setting. Set `--parallel-jobs` to an appropriate value to override the default based on your cluster configuration and observation.

  If CPU use is greater than 50-60%, you should lower the number of jobs. Similarly, if CPU use is low, you can increase the number of jobs.

   {{< note title="Note" >}}

   If there are timeouts during import, restart the import with fewer parallel jobs.

   {{< /note >}}

- **Increase batch size**. The default [--batch-size](../reference/data-migration/import-data/#arguments) is 20000 rows or approximately 200 MB of data, depending on whichever is reached first while preparing the batch. Normally this is considered a good default value. However, if the rows are too small, then you may consider increasing the batch size for greater throughput. Increasing the batch size to a very high value is not recommended as the whole batch is executed in one transaction.

- **Add disks** to reduce disk write contention. YugabyteDB servers can be configured with one or multiple disk volumes to store tablet data. If all tablets are writing to a single disk, write contention can slow down the ingestion speed. Configuring the [YB-TServers](../../reference/configuration/yb-tserver/) with multiple disks can reduce disk write contention, thereby increasing throughput. Disks with higher IOPS and better throughput also improve write performance.

- **Enable packed rows** ([Early Access](../../releases/versioning/#feature-maturity)) to increase the throughput by more than two times. Enable packed rows on the YugabyteDB cluster by setting the YB-TServer flag [ysql_enable_packed_row](../../reference/configuration/yb-tserver/#ysql-enable-packed-row) to true.

- **Configure the host machine's disk** with higher IOPS and better throughput to improve the performance of the splitter, which splits the large data file into smaller splits of 20000 rows. Splitter performance depends on the host machine's disk.

#### Performance test results

The following performance test results demonstrate the preceding techniques.

[Import data](../migrate/migrate-steps/#import-data) was tested using varying configurations, including more parallel jobs, multiple disks, and a larger cluster. The tests were run using a 28GB CSV file with 350 million rows on YugabyteDB version 2.16.0.0-b90.

The table schema for the test was as follows:

```sql
CREATE TABLE public.accounts (
    block bigint NOT NULL,
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
| 24 parallel jobs (default) | 3 node [RF](../../architecture/docdb-replication/replication/#replication-factor) 3 cluster,<br> c5.4x large (16 cores 32 GB) <br> 1 EBS Type gp3 disk per node,<br> 10000 IOPS,<br> 500 MiB bandwidth | batch-size=20k<br>parallel-jobs=24 | ~80% | 44014 rows/sec |
| Increase jobs<br>(1 per core) | 3 node RF 3 cluster,<br> c5.4x large (16 cores 32 GB) <br> 1 EBS Type gp3 disk per node,<br> 10000 IOPS,<br> 500 MiB bandwidth | batch-size=20k<br>parallel-jobs=48 | ~95% | 47696 rows/sec |
| Add nodes | 6 Node RF 3 cluster,<br> c5.4x large (16 cores 32GB) <br> 4 EBS Type gp3 disks per node,<br> 10000 IOPS,<br> 500 MiB bandwidth | batch-size=20k<br>parallel-jobs=48 | ~80% | 86547 rows/sec |
| Enabling packed rows | 3 node RF 3 cluster,<br> c5.4x large (16 cores 32 GB) <br> 1 EBS Type gp3 disk per node,<br> 10000 IOPS,<br> 500 MiB bandwidth | batch-size=20k<br>parallel-jobs=48<br>YB-TServer GFlag: `ysql_enable_packed_row` = `true` | ~95% | 134048 rows/sec |

{{< note title="Note" >}}

- The more optimizations you use, the more CPU you will require. Allowing CPU use greater than 60% may not be advisable as the database requires some cycles for internal operations.

- These performance optimizations apply whether you are importing data using the yb-voyager [import data command](../migrate/migrate-steps/#import-data) or the [import data file command](../migrate/bulk-data-load/#import-data-files-from-the-local-disk).

{{< /note >}}

## Improve export performance

By default, yb-voyager exports four tables at a time. To speed up data export, parallelize the export of data from multiple tables using the `–-parallel-jobs` argument with the export data command to increase the number of jobs. For details about the argument, refer to the [arguments table](../reference/data-migration/export-data/#arguments). Setting the value too high can, however, negatively impact performance; so a setting of 4 typically performs well.

If you use BETA_FAST_DATA_EXPORT to [accelerate data export](../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), yb-voyager exports only one table at a time and the `--parallel-jobs` argument is ignored.

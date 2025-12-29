---
title: Tune performance
linkTitle: Tune performance
description: Performance
menu:
  stable_yugabyte-voyager:
    identifier: performance
    parent: reference-voyager
    weight: 104
aliases:
  - /stable/yugabyte-voyager/performance/
  - /stable/yugabyte-voyager/monitor/performance/
type: docs
---

This page describes factors that can affect the performance of migration jobs being carried out using [yb-voyager](https://github.com/yugabyte/yb-voyager), along with the tuneable parameters you can use to improve performance.

## Improve import performance

There are several factors that slow down data-ingestion performance in any database:

- **Constraint checks**. Every insert has to satisfy the constraints (including foreign key constraints, value constraints, and so on) defined on a table, which results in extra processing. In distributed databases, this becomes pronounced as foreign key checks invariably mean talking to peer servers.

- **Trigger actions**. If triggers are defined on tables for every insert, then the corresponding trigger action (for each insert) is executed, which slows down ingestion.

yb-voyager improves performance when migrating data into a newly created empty database in several ways:

- Disables foreign key constraints during data import. However, other constraints like primary key constraints, check constraints, unique key constraints, and so on are not disabled. It's safe to disable some constraint checks as the data is from a reliable source. For maximum throughput, it is also preferable to not follow any order when populating tables.

- Disables triggers also during the import data phase.

  {{< note title="Note" >}}

yb-voyager only disables the constraint checks and triggers in the internal sessions it uses to migrate data.

  {{< /note >}}

### Techniques to improve performance

Use one or more of the following techniques to improve import data performance:

- **Load data in parallel**. yb-voyager imports batches from multiple tables at any given time using parallel connections. On YugabyteDB v2.20 and later, yb-voyager adjusts the number of connections based on the resource use (CPU and memory) of the cluster, with the goal of maintaining stability while optimizing CPU.

  Available flags:

  - By default, adaptive parallelism operates under moderate thresholds (`--adaptive-parallelism balanced`), where Voyager throttles the number of parallel connections if the CPU usage of any node exceeds 80%. To maximize CPU use for faster performance, you can use the `aggressive` flag; this is only recommended if you don't have any other running workloads.

  - By default, the upper bound for the number of parallel connections is set to half the total number of cores in the YugabyteDB cluster. Use the `--adaptive-parallelism-max` flag to override this value.

  - To disable adaptive parallelism and specify a static number of connections, use `--adaptive-parallelism disabled --parallel-jobs N`.

- **Increase batch size**. The default [--batch-size](../../reference/data-migration/import-data/#arguments) is 20000 rows or approximately 200 MB of data, depending on whichever is reached first while preparing the batch. Normally this is considered a good default value. However, if the rows are too small, then you may consider increasing the batch size for greater throughput. Increasing the batch size to a very high value is not recommended as the whole batch is executed in one transaction.

- **Add disks** to reduce disk write contention. YugabyteDB servers can be configured with one or multiple disk volumes to store tablet data. If all tablets are writing to a single disk, write contention can slow down the ingestion speed. Configuring the [YB-TServers](../../../reference/configuration/yb-tserver/) with multiple disks can reduce disk write contention, thereby increasing throughput. Disks with higher IOPS and better throughput also improve write performance.

- **Enable packed rows** to increase the throughput by more than two times. Enable packed rows on the YugabyteDB cluster by setting the YB-TServer flag [ysql_enable_packed_row](../../../reference/configuration/yb-tserver/#ysql-enable-packed-row) to true. In v2.20.0 and later, packed rows for YSQL is enabled by default for new clusters.

- **Configure the host machine's disk** with higher IOPS and better throughput to improve the performance of the splitter, which splits the large data file into smaller splits of 20000 rows. Splitter performance depends on the host machine's disk.

## Improve export performance

By default, yb-voyager exports four tables at a time. To speed up data export, parallelize the export of data from multiple tables using the `--parallel-jobs` argument with the export data command to increase the number of jobs. For details about the argument, refer to the [arguments table](../../reference/data-migration/export-data/#arguments). Setting the value too high can, however, negatively impact performance; so a setting of 4 typically performs well.

If you use BETA_FAST_DATA_EXPORT to [accelerate data export](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), yb-voyager exports only one table at a time and the `--parallel-jobs` argument is ignored.

---
title: Tune performance
linkTitle: Tune performance
description: Performance
menu:
  preview_yugabyte-voyager:
    identifier: performance
    parent: reference-voyager
    weight: 104
aliases:
  - /preview/yugabyte-voyager/performance/
  - /preview/yugabyte-voyager/monitor/performance/
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

- **Load data in parallel**. yb-voyager executes N parallel batch ingestion jobs at any given time. On YugabyteDB v2.20 and above, yb-voyager adapts the value of N depending on the resource usage (CPU/memory) of the cluster, with the goal of maintaining an optimal CPU usage (<70%). By default, the upper bound of N is set to half the total number of cores in the YugabyteDB cluster. Use the --adaptive-parallelism-max flag to override this default value.

  Against older YugabyteDB versions, N is equal to one-fourth of the total number of cores in the YugabyteDB cluster. Normally this is a good default value and should consume around 50-60% of CPU usage.

  If CPU use is greater than 50-60%, you should lower the number of jobs. Similarly, if CPU use is low, you can increase the number of jobs.

  Use the [--parallel-jobs](../../reference/data-migration/import-data/#arguments) argument with the import data command to override the default setting based on your cluster configuration and observation.

   {{< note title="Note" >}}

   If there are timeouts during import, restart the import with fewer parallel jobs.

   {{< /note >}}

- **Increase batch size**. The default [--batch-size](../../reference/data-migration/import-data/#arguments) is 20000 rows or approximately 200 MB of data, depending on whichever is reached first while preparing the batch. Normally this is considered a good default value. However, if the rows are too small, then you may consider increasing the batch size for greater throughput. Increasing the batch size to a very high value is not recommended as the whole batch is executed in one transaction.

- **Add disks** to reduce disk write contention. YugabyteDB servers can be configured with one or multiple disk volumes to store tablet data. If all tablets are writing to a single disk, write contention can slow down the ingestion speed. Configuring the [YB-TServers](../../../reference/configuration/yb-tserver/) with multiple disks can reduce disk write contention, thereby increasing throughput. Disks with higher IOPS and better throughput also improve write performance.

- **Enable packed rows** to increase the throughput by more than two times. Enable packed rows on the YugabyteDB cluster by setting the YB-TServer flag [ysql_enable_packed_row](../../../reference/configuration/yb-tserver/#ysql-enable-packed-row) to true. In v2.20.0 and later, packed rows for YSQL is enabled by default for new clusters.

- **Configure the host machine's disk** with higher IOPS and better throughput to improve the performance of the splitter, which splits the large data file into smaller splits of 20000 rows. Splitter performance depends on the host machine's disk.

## Improve export performance

By default, yb-voyager exports four tables at a time. To speed up data export, parallelize the export of data from multiple tables using the `--parallel-jobs` argument with the export data command to increase the number of jobs. For details about the argument, refer to the [arguments table](../../reference/data-migration/export-data/#arguments). Setting the value too high can, however, negatively impact performance; so a setting of 4 typically performs well.

If you use BETA_FAST_DATA_EXPORT to [accelerate data export](../../migrate/migrate-steps/#accelerate-data-export-for-mysql-and-oracle), yb-voyager exports only one table at a time and the `--parallel-jobs` argument is ignored.

---
title: Best practices for YSQL DB administrators
headerTitle: Best practices
linkTitle: Best practices
description: Tips and tricks to build YSQL applications
headcontent: Tips and tricks to administer YSQL DBs
menu:
  stable:
    identifier: best-practices-ysql-db-admins
    parent: best-practices-ysql
    weight: 570
type: docs
---

## Single availability zone (AZ) deployments

In single AZ deployments, you need to set the [yb-tserver](../../reference/configuration/yb-tserver) flag `--durable_wal_write=true` to not lose data if the whole data center goes down (For example, power failure).

## Allow for tablet replica overheads

Although you can manually provision the amount of memory each TServer uses using flags ([--memory_limit_hard_bytes](../../reference/configuration/yb-tserver/#memory-limit-hard-bytes) or [--default_memory_limit_to_ram_ratio](../../reference/configuration/yb-tserver/#default-memory-limit-to-ram-ratio)), this can be tricky as you need to take into account how much memory the kernel needs, along with the PostgreSQL processes and any Master process that is going to be colocated with the TServer.

Accordingly, you should use the [--use_memory_defaults_optimized_for_ysql](../../reference/configuration/yb-tserver/#use-memory-defaults-optimized-for-ysql) flag, which gives good memory division settings for using YSQL, optimized for your node's size.

If this flag is true, then the [memory division flag defaults](../../reference/configuration/yb-tserver/#memory-division-flags) change to provide much more memory for PostgreSQL; furthermore, they optimize for the node size.

Note that although the default setting is false, when creating a new universe using yugabyted or YugabyteDB Anywhere, the flag is set to true, unless you explicitly set it to false.

## Settings for CI and CD integration tests

You can set certain flags to increase performance using YugabyteDB in CI and CD automated test scenarios as follows:

- Point the flags `--fs_data_dirs`, and `--fs_wal_dirs` to a RAMDisk directory to make DML, DDL, cluster creation, and cluster deletion faster, ensuring that data is not written to disk.
- Set the flag `--yb_num_shards_per_tserver=1`. Reducing the number of shards lowers overhead when creating or dropping YSQL tables, and writing or reading small amounts of data.
- Use colocated databases in YSQL. Colocation lowers overhead when creating or dropping YSQL tables, and writing or reading small amounts of data.
- Set the flag `--replication_factor=1` for test scenarios, as keeping the data three way replicated (default) is not necessary. Reducing that to 1 reduces space usage and increases performance.
- Use `TRUNCATE table1,table2,table3..tablen;` instead of CREATE TABLE, and DROP TABLE between test cases.

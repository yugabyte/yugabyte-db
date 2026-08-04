---
title: Best practices for YSQL database administrators
headerTitle: Best practices for YSQL database administrators
linkTitle: YSQL database administrators
description: Tips and tricks to build YSQL applications
headcontent: Tips and tricks for administering YSQL databases
menu:
  v2024.1:
    identifier: best-practices-ysql-administration
    parent: best-practices-operations
    weight: 10
type: docs
---

Database administrators can fine-tune YugabyteDB deployments for better reliability, performance, and operational efficiency by following targeted best practices. This guide outlines key recommendations for configuring single-AZ environments, optimizing memory use, accelerating CI/CD tests, and safely managing concurrent DML and DDL operations. These tips are designed to help DBAs maintain stable, scalable YSQL clusters in real-world and test scenarios alike.

## Single availability zone (AZ) deployments

In single AZ deployments, you need to set the [yb-tserver](../../reference/configuration/yb-tserver) flag `--durable_wal_write=true` to not lose data if the whole data center goes down (for example, power failure).

## Allow for tablet replica overheads

For a universe with [RF3](../../architecture/key-concepts/#replication-factor-rf), 1000 tablets imply 3000 tablet replicas. If the universe has three nodes, then each node has on average 1000 tablet replicas. A six node universe would have on average 500 tablet replicas per-node and so on.

Each 1000 tablet replicas on a node impose an overhead of 0.4 vCPUs for Raft heartbeats (assuming a 0.5 second heartbeat interval), 800 MiB of memory, and 128 GB of storage space for write-ahead logs (WALs).

The overhead is proportional to the number of tablet replicas so 500 tablet replicas would need half as much.

Additional memory will be required for supporting caches and the like if the tablets are being actively used. We recommend provisioning an extra 6200 MiB of memory for each 1000 tablet replicas on a node to handle these cases; that is, a TServer should have 7000 MiB of RAM allocated to it for each 1000 tablet replicas it may be expected to support.

Manually provisioning the amount of memory each TServer uses can be done using the [--memory_limit_hard_bytes](../../reference/configuration/yb-tserver/#memory-limit-hard-bytes) or [--default_memory_limit_to_ram_ratio](../../reference/configuration/yb-tserver/#default-memory-limit-to-ram-ratio) flags.  Manually provisioning is a bit tricky as you need to take into account how much memory the kernel needs as well as the postgres and any master process that is going to be colocated with the TServer.

{{<note title = "Kubernetes deployments">}}
For Kubernetes universes, memory limits are controlled via resource specifications in the Helm chart. Accordingly, `--default_memory_limit_to_ram_ratio` does not apply, and `--memory_limit_hard_bytes` is automatically set from the Kubernetes pod memory limits.

See [Memory limits in Kubernetes deployments](../../deploy/kubernetes/single-zone/oss/helm-chart/#memory-limits-for-kubernetes-deployments) for details.
{{</note>}}

Accordingly, it is recommended that you instead use the [--use_memory_defaults_optimized_for_ysql](../../reference/configuration/yb-tserver/#use-memory-defaults-optimized-for-ysql) flag, which gives you good memory division settings for using YSQL optimized for your node's size. Consult the table showing node RAM versus maximum tablet replicas to see how big of a node you will need based on how many tablet replicas per server you want supported.

## Settings for CI and CD integration tests

You can set certain flags to increase performance using YugabyteDB in CI and CD automated test scenarios as follows:

- Point the flags `--fs_data_dirs`, and `--fs_wal_dirs` to a RAMDisk directory to make DML, DDL, cluster creation, and cluster deletion faster, ensuring that data is not written to disk.
- Set the flag `--yb_num_shards_per_tserver=1`. Reducing the number of shards lowers overhead when creating or dropping YSQL tables, and writing or reading small amounts of data.
- Use colocated databases in YSQL. Colocation lowers overhead when creating or dropping YSQL tables, and writing or reading small amounts of data.
- Set the flag `--replication_factor=1` for test scenarios, as keeping the data three way replicated (default) is not necessary. Reducing that to 1 reduces space usage and increases performance.
- Use `TRUNCATE table1,table2,table3..tablen;` instead of CREATE TABLE, and DROP TABLE between test cases.

## Concurrent DML during a DDL operation

In YugabyteDB, DML is allowed to execute while a DDL statement modifies the schema that is accessed by the DML statement. For example, an `ALTER TABLE <table> .. ADD COLUMN` DDL statement may add a new column while a `SELECT * from <table>` executes concurrently on the same relation. In PostgreSQL, this is typically not allowed because such DDL statements take a table-level exclusive lock that prevents concurrent DML from executing. (Support for similar behavior in YugabyteDB is being tracked in issue {{<issue 11571>}}.)

In YugabyteDB, when a DDL modifies the schema of tables that are accessed by concurrent DML statements, the DML statement may do one of the following:

- Operate with the old schema prior to the DDL.
- Operate with the new schema after the DDL completes.
- Encounter temporary errors such as `schema mismatch errors` or `catalog version mismatch`. It is recommended for the client to [retry such operations](https://www.yugabyte.com/blog/retry-mechanism-spring-boot-app/) whenever possible.

Most DDL statements complete quickly, so this is typically not a significant issue in practice. However, [certain kinds of ALTER TABLE DDL statements](../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite) involve making a full copy of the table(s) whose schema is being modified. For these operations, it is not recommended to run any concurrent DML statements on the table being modified by the `ALTER TABLE`, as the effect of such concurrent DML may not be reflected in the table copy.

## Concurrent DDL during a DDL operation

DDL statements that affect entities in different databases can be run concurrently. However, for DDL statements that impact the same database, it is recommended to execute them sequentially.

DDL statements that relate to shared objects, such as roles or tablespaces, are considered as affecting all databases in the cluster, so they should also be run sequentially.

## Preload PostgreSQL system catalog entries into the local catalog cache

Many common PostgreSQL operations, such as parsing a query, planning, and so on, require looking up entries in PostgreSQL system catalog tables, including pg_class, pg_operator, pg_statistic, and pg_attribute, for PostgreSQL metadata for the columns, operators, and more.

Each PostgreSQL backend (process) caches such metadata for performance reasons. In YugabyteDB, misses on these caches need to be loaded from the YB-Master leader. As a result, initial queries on that backend can be slow until these caches are warm, especially if the YB-Master leader is in a different region.

You can customize this tradeoff to control the preloading entries into PostgreSQL caches. Refer to [Customize preloading of YSQL catalog caches](../ysql-catalog-cache-tuning-guide/) for information on how to make the right tradeoffs for your application.

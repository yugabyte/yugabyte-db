---
title: Best practices for YSQL database administrators
headerTitle: Best practices for YSQL database administrators
linkTitle: YSQL database administrators
description: Tips and tricks to build YSQL applications
headcontent: Tips and tricks for administering YSQL databases
menu:
  stable:
    identifier: best-practices-ysql-administration
    parent: best-practices-operations
    weight: 10
aliases:
  - /stable/develop/best-practices/administration/
type: docs
rightNav:
  hideH3: true
---

Database administrators can fine-tune YugabyteDB deployments for better reliability, performance, and operational efficiency by following targeted best practices. This guide outlines key recommendations for configuring single-AZ environments, optimizing memory use, accelerating CI/CD tests, and safely managing concurrent DML and DDL operations. These tips are designed to help DBAs maintain stable, scalable YSQL clusters in real-world and test scenarios alike.

## Single availability zone (AZ) deployments

In single AZ deployments, you need to set the [yb-tserver](../../reference/configuration/yb-tserver) flag `--durable_wal_write=true` to not lose data if the whole data center goes down (for example, power failure).

## Allow for tablet replica overheads

Although you can manually provision the amount of memory each TServer uses using flags ([--memory_limit_hard_bytes](../../reference/configuration/yb-tserver/#memory-limit-hard-bytes) or [--default_memory_limit_to_ram_ratio](../../reference/configuration/yb-tserver/#default-memory-limit-to-ram-ratio)), this can be tricky as you need to take into account how much memory the kernel needs, along with the PostgreSQL processes and any Master process that is going to be colocated with the TServer.

{{<note title = "Kubernetes deployments">}}
For Kubernetes universes, memory limits are controlled via resource specifications in the Helm chart. Accordingly, `--default_memory_limit_to_ram_ratio` does not apply, and `--memory_limit_hard_bytes` is automatically set from the Kubernetes pod memory limits.

See [Memory limits in Kubernetes deployments](../../deploy/kubernetes/single-zone/oss/helm-chart/#memory-limits-for-kubernetes-deployments) for details.
{{</note>}}

Accordingly, you should use the [--use_memory_defaults_optimized_for_ysql](../../reference/configuration/yb-tserver/#use-memory-defaults-optimized-for-ysql) flag, which gives good memory division settings for using YSQL, optimized for your node's size.

If this flag is true, then the [memory division flag defaults](../../reference/configuration/yb-tserver/#memory-division-flags) change to provide much more memory for PostgreSQL; furthermore, they optimize for the node size.

Note that although the default setting is false, when creating a new universe using yugabyted or YugabyteDB Anywhere, the flag is set to true, unless you explicitly set it to false.

For more details, refer to [Memory and tablet limits](../../deploy/checklist/#memory-and-tablet-limits).

## Settings for CI and CD integration tests

You can set certain flags to increase performance using YugabyteDB in CI and CD automated test scenarios as follows:

- Point the flags `--fs_data_dirs`, and `--fs_wal_dirs` to a RAMDisk directory to make DML, DDL, cluster creation, and cluster deletion faster, ensuring that data is not written to disk.
- Set the flag `--yb_num_shards_per_tserver=1`. Reducing the number of shards lowers overhead when creating or dropping YSQL tables, and writing or reading small amounts of data.
- Use colocated databases in YSQL. Colocation lowers overhead when creating or dropping YSQL tables, and writing or reading small amounts of data.
- Set the flag `--replication_factor=1` for test scenarios, as keeping the data three way replicated (default) is not necessary. Reducing that to 1 reduces space usage and increases performance.
- Use `TRUNCATE table1,table2,table3..tablen;` instead of CREATE TABLE, and DROP TABLE between test cases.

## Concurrent DML during a DDL operation

By default, YugabyteDB doesn't restrict DML and DDL concurrency. As a result, a DML statement can potentially operate using the old (prior to the DDL) or new schema; for example, an `ALTER TABLE <table> .. ADD COLUMN` DDL statement may add a new column while a `SELECT * from <table>` executes concurrently on the same relation. This can cause the following problems:

- Errors such as `schema mismatch errors` or `catalog version mismatch`. The client should [retry such operations](https://www.yugabyte.com/blog/retry-mechanism-spring-boot-app/) whenever possible.
- Inconsistencies. For example:

    - A table rewrite like `ALTER TABLE ADD COLUMN c int DEFAULT random()` may create a rewritten table that does not include some concurrently written rows in the old table.
    - `CREATE INDEX NONCONCURRENTLY` may create an index that does not include some concurrently written rows from the old table. For more information, see [Concurrent index creation](../../api/ysql/the-sql-language/statements/ddl_create_index/#semantics).
    - `ALTER TABLE ADD CONSTRAINT c NOT NULL` may still have some NULL entries for `c` if they were written concurrent to this DDL.
    - Concurrent writes during `ALTER TABLE partition_parent ATTACH child` or `CREATE TABLE partition_child PARTITION OF parent` for the newly attached partition column range may end up in the default partition.

To avoid this, you can:

- manually pause writes during the DDL workload; or
- enable {{<tags/feature/ea idea="1114">}}[table-level locking](../../architecture/transactions/concurrency-control/#table-level-locks).

For specific DDLs, like `ALTER TABLE ADD CONSTRAINT`, an alternate workaround is to perform the action in two steps:

```sql
ALTER TABLE ADD CONSTRAINT ... INVALID
ALTER TABLE ... VALIDATE CONSTRAINT
```

This should be safe to do even without table locking enabled.

For `ALTER TABLE partition_parent ATTACH child`, you can first add `CHECK` constraints to the existing partitions that exclude the newly attached range of partition columns from the existing partitions. This can guarantee that no rows are present in or can be inserted for this range to the existing partitions before adding the new partition.

Most DDL statements complete quickly, so this is typically not a significant issue in practice. However, [certain kinds of ALTER TABLE DDL statements](../../api/ysql/the-sql-language/statements/ddl_alter_table/#alter-table-operations-that-involve-a-table-rewrite) involve making a full copy of the table(s) whose schema is being modified. For these operations, it is not recommended to run any concurrent DML statements on the table being modified by the `ALTER TABLE`, as the effect of such concurrent DML may not be reflected in the table copy.

## Concurrent DDL during a DDL operation

Concurrent Data Definition Language (DDL) operations are currently unsupported. All DDL statements targeting the same database must be executed sequentially, one at a time, from a single database connection. DDL statements that operate on shared objects (roles, tablespaces) affect all databases in the cluster and must also be serialized. DDL statements that affect entities in different databases can be run concurrently.

Enforce DDL serialization at the application and operational level:

- Execute all DDLs sequentially from a single connection. Use a dedicated, non-pooled connection for schema migrations.
- Wait for each DDL to fully complete before issuing the next statement.
- Implement client-side retry logic for schema mismatch and catalog version mismatch errors in any [DML that may overlap with DDL windows](#concurrent-dml-during-a-ddl-operation).
- Schedule DDL during maintenance windows to minimize overlap with application DML traffic, backup jobs, and other administrative operations.
- In versions earlier than v2025.1.1, DDL verification states can block backup and restore operations; run DDL and backup jobs separately. (In v2025.2.1 and later, taking YSQL backups during DDL operations is supported by default, and backups succeed even in case of concurrent DDLs.)

## Preload PostgreSQL system catalog entries into the local catalog cache

Many common PostgreSQL operations, such as parsing a query, planning, and so on, require looking up entries in PostgreSQL system catalog tables, including pg_class, pg_operator, pg_statistic, and pg_attribute, for PostgreSQL metadata for the columns, operators, and more.

Each PostgreSQL backend (process) caches such metadata for performance reasons. In YugabyteDB, misses on these caches need to be loaded from the YB-Master leader. As a result, initial queries on that backend can be slow until these caches are warm, especially if the YB-Master leader is in a different region.

You can customize this tradeoff to control the preloading entries into PostgreSQL caches. Refer to [Customize preloading of YSQL catalog caches](../ysql-catalog-cache-tuning-guide/) for information on how to make the right tradeoffs for your application.

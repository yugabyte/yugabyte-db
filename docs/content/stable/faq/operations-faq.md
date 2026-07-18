---
title: FAQs about operating YugabyteDB clusters
headerTitle: Operations FAQ
linkTitle: Operations FAQ
description: Answers to common questions about operating YugabyteDB clusters
menu:
  stable_faq:
    identifier: faq-operations
    parent: faq
    weight: 50
type: docs
unversioned: true
rightNav:
  hideH4: true
---

## General

### Do YugabyteDB clusters need an external load balancer?

For YSQL, you should use a YugabyteDB smart driver. YugabyteDB smart drivers automatically balance connections to the database and eliminate the need for an external load balancer. If you are not using a smart driver, you will need an external load balancer.

{{<lead link="/stable/develop/drivers-orms/smart-drivers/">}}
YugabyteDB smart drivers for YSQL
{{</lead>}}

For YCQL, YugabyteDB provides automatic load balancing.

[YugabyteDB Aeon](../../yugabyte-cloud/) clusters automatically use the uniform load balancing provided by the cloud provider where the cluster is provisioned. YugabyteDB Aeon creates an external load balancer to distribute the connection load across the nodes in a particular region. For multi-region clusters, each region has its own external load balancer. For regular connections, you need to connect to the region of choice, and application connections are then uniformly distributed across the region without the need for any special coding.

{{<lead link="/stable/develop/drivers-orms/smart-drivers/#using-smart-drivers-with-yugabytedb-aeon">}}
Connection load balancing in YugabyteDB Aeon
{{</lead>}}

#### Using GCP load balancers

To configure a YugabyteDB universe deployed on GCP to use GCP-provided load balancers, you must set the [--pgsql_proxy_bind_address 0.0.0.0:5433](../../reference/configuration/yb-tserver/#pgsql-proxy-bind-address) and [--cql_proxy_bind_address 0.0.0.0:9042](../../reference/configuration/yb-tserver/#cql-proxy-bind-address) flags.

{{<lead link="../../yugabyte-platform/manage-deployments/edit-config-flags/">}}
Edit configuration flags
{{</lead>}}

### Can write ahead log (WAL) files be cleaned up or reduced in size?

For most YugabyteDB deployments, you should not need to adjust the configuration flags for the write ahead log (WAL). While your data size is small and growing, the WAL files may seem to be much larger, but over time, the WAL files should reach their steady state while the data size continues to grow and become larger than the WAL files.

Note that you should not delete any file in the `yb-data` folder, including the WAL files.

WAL files are per tablet and the retention policy is managed by the following two yb-tserver configuration flags:

- [`--log_min_segments_to_retain`](../../reference/configuration/yb-tserver/#log-min-segments-to-retain)
- [`--log_min_seconds_to_retain`](../../reference/configuration/yb-tserver/#log-min-seconds-to-retain)

Also, the following yb-tserver configuration flag is a factor in the size of each WAL file before it is rolled into a new one:

- [`--log_segment_size_mb`](../../reference/configuration/yb-tserver/#log-segment-size-mb) – default is `64`.

### How do I determine the size of a YSQL database?

YugabyteDB doesn't currently support the `pg_database_size` function. Instead, use a custom function based on `pg_table_size` to calculate the size of the database.

```sql
CREATE OR REPLACE FUNCTION yb_pg_database_size()
  RETURNS BIGINT
AS
$$
DECLARE
  sql_statement RECORD;
  ObjectSize BIGINT := 0;
  DataBaseSize BIGINT := 0;
BEGIN
  -- Tables
  FOR sql_statement IN
    SELECT 'SELECT pg_table_size(''' || schemaname || '.' || tablename || ''');' AS ddl FROM pg_tables WHERE schemaname NOT IN ('information_schema', 'pg_catalog')
  LOOP
    EXECUTE sql_statement.ddl INTO ObjectSize;
    DataBaseSize := DataBaseSize + ObjectSize;
  END LOOP;

  -- Indexes
  FOR sql_statement IN
  SELECT 'SELECT pg_table_size(''' || n.nspname || '.' || c.relname || ''');' AS ddl FROM pg_catalog.pg_class c LEFT JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace LEFT JOIN pg_catalog.pg_index i ON i.indexrelid = c.oid WHERE n.nspname NOT IN ('pg_catalog', 'information_schema') AND c.relkind IN ('i','I','') AND NOT i.indisprimary
  LOOP
    EXECUTE sql_statement.ddl INTO ObjectSize;
    DataBaseSize := DataBaseSize + ObjectSize;
  END LOOP;

  RETURN DataBaseSize;

END;
$$
LANGUAGE plpgsql;
```

Now, when you run,

```sql
SELECT yb_pg_database_size() as size_bytes, yb_pg_database_size()/1048576 as size_mb;
```

You should see output like the following:

```caddyfile{.nocopy}
 size_bytes | size_mb
------------+---------
   80540743 |      76
```

{{<lead link="https://yugabytedb.tips/display-ysql-database-size/">}}
For more information, see [Display YSQL Database size](https://yugabytedb.tips/display-ysql-database-size/)
{{</lead>}}

### How can I create a user in YSQL with a password that expires after a specific time interval?

You can create a user with a password that expires after a set time using the `VALID UNTIL` clause. The following example sets the expiration time 4 hours from the current time:

```plpgsql
DO $$
DECLARE time TIMESTAMP := now() + INTERVAL '4 HOURS';
BEGIN
  EXECUTE format(
    'CREATE USER "John" WITH PASSWORD ''secure_password'' VALID UNTIL ''%s'';',
    time
  );
END
$$;
```

To verify the password expiration time, run the following query:

```plpgsql
SELECT now(), valuntil, valuntil - now() AS diff
FROM pg_user
WHERE usename = 'John';
```

```output
             now              |        valuntil        |      diff
------------------------------+------------------------+----------------
 2025-01-23 17:16:22.82708+00 | 2025-01-23 21:16:21+00 | 03:59:58.17292
(1 row)
```

This confirms that the password for `John` will expire in approximately 4 hours.

## Upgrade

### What exactly does the upgrade with rollback feature do?

The feature upgrades the binaries and enables new features that do not change the data format on disk. Features that do require changes to the format of data sent over the network, or stored on disk, are disabled until you finalize the upgrade.

Because the vast majority of new changes do not modify the data format, and most changes that do are from new features that are not yet in use, this temporary disabling typically has no impact on current operations. This allows you to monitor the new version for a while (validating typically about 90% of code changes) to ensure the upgrade has not impacted your application.

If you discover any regressions to existing features or query plans, rollback allows you to handle them quickly by reverting to the previous binary without any data corruption.

{{<lead link="../../yugabyte-platform/manage-deployments/upgrade-software-install/">}}
For detailed information, see [Upgrade YugabyteDB](../../manage/upgrade-deployment/).
{{</lead>}}

### How long can a universe run after the upgrade but before the finalize step?

A universe can run as long as needed while the application is being validated. However, it is recommended to finalize within 2 days. Operations like flag changes are disabled during the monitoring phase.

### Is it possible to run DDL operations during the upgrade (before finalize)?

DDL operations are allowed during regular upgrades (major and minor). DDL operations are only _blocked_ during YSQL major upgrades, where the PostgreSQL version is also upgraded, such as from v2024.2 (PostgreSQL 11) to v2025.1 (PostgreSQL 15).

### Assuming rollback is not possible post-finalize, what do I do if I discover problems after finalization?

Yugabyte performs extensive testing to ensure there are no issues. After the data format on disk has changed, you cannot go back to old binaries; this is a technical limitation of any software that stores data to disk. The only way to roll back after finalize is to restore from a backup that was taken before the finalization (with loss of data).

For customers that are extremely risk averse, the recommendation is to upgrade and finalize a development environment and DR replicas (if any) first.

### How does rollback interact with xCluster setups?

You can upgrade and roll back each cluster individually.

xCluster can only replicate from an old binary version to the new binary version. You should finalize the target universe before the source universe. If the source is finalized before the target, then xCluster automatically pauses itself (this only happens in certain versions that have an external data format change, such as v2024.2).

For bidirectional setups, if writes are only happening on one side, then you can upgrade and finalize the other side first and then upgrade the writing side. If both sides are taking writes, then both should be finalized at the same time; otherwise, replication in the new-to-old direction will be paused.

{{<lead link="../../yugabyte-platform/manage-deployments/xcluster-replication/bidirectional-replication/">}}
For more information about bidirectional xCluster replication, see [Bidirectional replication using xCluster](../../yugabyte-platform/manage-deployments/xcluster-replication/bidirectional-replication/).
{{</lead>}}

### Are the xCluster bidirectional upgrade steps the same for both YSQL and YCQL?

Yes.

### What is the behavior of AutoFlags during an upgrade?

YugabyteDB Anywhere enables Volatile AutoFlags after all nodes are running the new binary version (before finalize). Only Persisted and External AutoFlags are enabled after finalization.

{{<lead link="https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md">}}
For detailed information about AutoFlags, see [AutoFlags](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md).
{{</lead>}}

### If not all new features are required immediately, is there a way to selectively enable them post-finalize?

Features have dedicated flags to allow you to tune or disable them. For example, cost-based optimizer has a flag that lets you pick the mode, and tablet split has threshold flags that can be set.

Refer to the documentation of the specific feature for more information on how to tune them.

### What happens to custom flags set at the universe level?

Custom flags set at the universe level persist across the upgrade and finalize steps.

{{<lead link="../../yugabyte-platform/manage-deployments/edit-config-flags/">}}
For information about editing configuration flags, see [Edit configuration flags](../../yugabyte-platform/manage-deployments/edit-config-flags/).
{{</lead>}}

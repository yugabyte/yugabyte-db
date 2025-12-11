---
title: YSQL major upgrade - manual
headerTitle: YSQL major upgrade
linkTitle: YSQL major upgrade
description: Upgrade YugabyteDB to PostgreSQL 15 using manual installation
headcontent: Upgrade YugabyteDB to a version that supports PG 15
menu:
  v2025.1:
    identifier: ysql-major-upgrade-2
    parent: manage-upgrade-deployment
    weight: 706
type: docs
---

Upgrading YugabyteDB from a version based on PostgreSQL 11 (all versions prior to v2.25) to a version based on PostgreSQL 15 (v2025.1 or later (stable) or v2.25 or later (preview)) requires additional steps. For instructions on upgrades within a major PostgreSQL version, refer to [Upgrade YugabyteDB](../upgrade-deployment/).

The upgrade is fully online. While the upgrade is in progress, you have full and uninterrupted read and write access to your cluster.

Performing a YSQL major upgrade on a universe with [CDC with logical replication](../../additional-features/change-data-capture/using-logical-replication/) requires additional steps.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-major-upgrade-yugabyted/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>

  <li>
    <a href="../ysql-major-upgrade-local/" class="nav-link active">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>

  <li>
    <a href="../ysql-major-upgrade-logical-replication/" class="nav-link">
      <i class="icon-shell"></i>
      Logical Replication
    </a>
  </li>

</ul>

## Before you begin

- All DDL statements, except ones related to Temporary table and Refresh Materialized View, are blocked for the duration of the upgrade. Consider executing all DDLs before the upgrade, and pause any jobs that might run DDLs. DMLs are allowed.
- Upgrade client drivers.

    Upgrade all application client drivers to the new version. The client drivers are backwards compatible, and work with both the old and new versions of the database.
- Your cluster must be running v2024.2.3.0 or later.

    If you have a pre-existing cluster, first upgrade it to the latest version in the v2024.2 series using the [upgrade instructions](../upgrade-deployment/).

- If your cluster has dedicated YB-Master nodes (that is, nodes with YB-Master service only and no YB-TServer), you must create a superuser named `yugabyte_upgrade` and add its credentials to the `.pgpass` file on each YB-Master node. You can safely remove this user after the upgrade process is complete.

    ```sql
    CREATE USER yugabyte_upgrade WITH SUPERUSER PASSWORD '<strong_password>';
    ```

- If you have PITR enabled, delete the configuration before performing the upgrade. Recreate it only after the major upgrade is either finalized or rolled back.

### Precheck

New PostgreSQL major versions add many new features and performance improvements, but also remove some older unsupported features and data types. You can only upgrade after you remove all deprecated features and data types from your databases.

Use the [pg_upgrade](https://www.postgresql.org/docs/15/pgupgrade.html) tool provided with v2.25.1.0 to make sure your cluster is compatible with the new version.

Use the `--check` option as follows:

```sh
pg_upgrade --check -U <superuser_name> --old-host <any_yb_node_address>  --old-port <port> --old-datadir <yb_data_dir>
```

For example:

```sh
$ ./path_to_new_version/postgres/bin/pg_upgrade --check -U yugabyte --old-host 127.0.0.1 --old-port 5433 --old-datadir ~/yugabyte-data/node-1/disk-1/pg_data_11
```

```output
Performing Consistency Checks on Old Live Server
------------------------------------------------
Checking cluster versions                                   ok
Checking attributes of the 'yugabyte' user                  ok
Checking for all 3 system databases                         ok
Checking database connection settings                       ok
Checking for system-defined composite types in user tables  ok
Checking for reg* data types in user tables                 ok
Checking for user-defined postfix operators                 ok
Checking for incompatible polymorphic functions             ok
Checking for invalid "sql_identifier" user columns          ok

*Clusters are compatible*
```

{{<tip title="Backup">}}
Back up your cluster at this time. Refer to [Distributed snapshots for YSQL](../backup-restore/snapshot-ysql/).
{{</tip>}}

In addition, refer to the following:

- [Prepare your cluster for upgrade](../upgrade-deployment/#1-prepare-the-cluster-for-the-upgrade)
- [Download and install the new version](../upgrade-deployment/#2-download-and-install-the-new-version)

## Upgrade phase

### Enable mixed mode

Because the upgrade is fully online, your cluster will temporarily consist of a mix of nodes running both PostgreSQL 11 and 15 (mixed mode). These processes need to be able to talk with each other and correctly process your SQL commands. Specifically, the PostgreSQL expressions that are used in the SQL statements get pushed down from the compute layer to the YugabyteDB storage layer. Because the expressions used by PostgreSQL have changed across the major version, you need to disable this optimization during the upgrade. (This will be addressed in v2025.1 (issue {{<issue 24730>}}).)

Set the `ysql_yb_major_version_upgrade_compatibility` flag to `11` on all YB-Master and YB-TServer processes. For example:

```sh
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:7100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:7100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:7100 ysql_yb_major_version_upgrade_compatibility 11

$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:9100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:9100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:9100 ysql_yb_major_version_upgrade_compatibility 11
```

{{<tip title="Backup">}}
It is recommended to back up the cluster at this time. Refer to [Distributed snapshots for YSQL](../backup-restore/snapshot-ysql/).
{{</tip>}}

### Upgrade YB-Masters

[Upgrade the YB-Master](../upgrade-deployment/#3-upgrade-yb-masters) processes one at a time. Make sure `ysql_yb_major_version_upgrade_compatibility` remains set to 11.

### Upgrade YSQL catalog to the new version

PostgreSQL major version changes also involve breaking changes to the catalog.

Before the YB-TServers (and hence all PostgreSQL processes) are upgraded, you must make the YSQL catalog available in the new PostgreSQL 15 compatible format.

Use the yb-admin command `upgrade_ysql_major_version_catalog` to upgrade the YSQL catalog as follows:

```sh
yb-admin --master_addresses <master_addresses> upgrade_ysql_major_version_catalog
```

For example:

```sh
./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 upgrade_ysql_major_version_catalog
```

```output
ysql major catalog upgrade started
ysql major catalog upgrade completed successfully
```

### Upgrade all YB-TServers

[Upgrade the YB-TServer processes](../upgrade-deployment/#4-upgrade-yb-tservers) one at a time. Make sure `ysql_yb_major_version_upgrade_compatibility` remains set to 11.

Closely monitor your applications at this time. If any issues arise, you can [roll back](#rollback-phase) to the previous version. You can then address the issue and then retry the upgrade.

### Disable mixed mode

Now that all the YB-Master and YB-TServer processes are on the same version, you can disable mixed mode by setting the `ysql_yb_major_version_upgrade_compatibility` flag. This allows you to re-enable the pushdown optimizations.

  ```sh
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:7100 ysql_yb_major_version_upgrade_compatibility 0
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:7100 ysql_yb_major_version_upgrade_compatibility 0
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:7100 ysql_yb_major_version_upgrade_compatibility 0

  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:9100 ysql_yb_major_version_upgrade_compatibility 0
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:9100 ysql_yb_major_version_upgrade_compatibility 0
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:9100 ysql_yb_major_version_upgrade_compatibility 0
  ```

## Monitor phase

After all the YB-Master and YB-TServer processes are upgraded, monitor the cluster to ensure it is healthy. Make sure workloads are running as expected and there are no errors in the logs.

You can remain in this phase for as long as you need, up to a _maximum recommended limit of two days_ to avoid operator errors that can arise from having to maintain two versions.

DDLs are not allowed even in this phase. New features that require format changes will not be available until the upgrade is finalized. Also, you cannot perform another upgrade until you have completed the current one.

If you are satisfied with the new version, proceed to [finalize the upgrade](#finalize-phase).

If you encounter any issues, you can [roll back](#rollback-phase) the upgrade.

## Finalize phase

Use yb-admin to finalize the upgrade.

You need to run the [finalize_upgrade](../../admin/yb-admin/#finalize-upgrade) command as follows:

```sh
yb-admin --master_addresses <master_addresses> finalize_upgrade
```

Note that `finalize_upgrade` is a cluster-level operation; you don't need to run it on every node.

## Rollback phase

### Enable mixed mode

Set the `ysql_yb_major_version_upgrade_compatibility` flag to `11` on all YB-Master and YB-TServer processes. For example:

```sh
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:7100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:7100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:7100 ysql_yb_major_version_upgrade_compatibility 11

$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:9100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:9100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:9100 ysql_yb_major_version_upgrade_compatibility 11
```

### Roll back YB-TServers

[Roll back all YB-TServers](../upgrade-deployment/#1-roll-back-yb-tservers).

### Roll back the catalog

Use the yb-admin command `rollback_ysql_major_version_catalog` to roll back the YSQL catalog as follows:

```sh
yb-admin --master_addresses <master_addresses> rollback_ysql_major_version_catalog
```

For example:

```sh
./yugabyte-2.25.1.0/bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 rollback_ysql_major_version_catalog
```

```output
Rollback successful
```

### Roll back YB-Masters

[Roll back all YB-Masters](../upgrade-deployment/#2-roll-back-yb-masters) to v2024.2.3.0.

If you are using the cost based optimizer, run ANALYZE on your databases to get the most optimal query plans.

### Disable mixed mode

Now that all the YB-Master and YB-TServer processes are on the same version, you can disable mixed mode by setting the `ysql_yb_major_version_upgrade_compatibility` flag.

  ```sh
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:7100 ysql_yb_major_version_upgrade_compatibility 0
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:7100 ysql_yb_major_version_upgrade_compatibility 0
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:7100 ysql_yb_major_version_upgrade_compatibility 0

  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:9100 ysql_yb_major_version_upgrade_compatibility 0
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:9100 ysql_yb_major_version_upgrade_compatibility 0
  $ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:9100 ysql_yb_major_version_upgrade_compatibility 0
  ```

## Post upgrade phase

- If you are using the cost based optimizer, run ANALYZE after the upgrade. {{<issue 25721>}}

## Limitations

- Expression pushdown is not available. {{<issue 24730>}}
- Upgrading with extensions is not yet supported. {{<issue 24733>}}
- Any backups that are taken in the monitoring phase can only be restored on a PG15 compatible universe (that is, backups cannot be restored if rollback is performed).

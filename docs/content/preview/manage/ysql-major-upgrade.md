---
title: YSQL major upgrade
headerTitle: YSQL major upgrade
linkTitle: YSQL major upgrade
description: Upgrade YugabyteDB to PostgreSQL 15
headcontent: Upgrade YugabyteDB to a version that supports PG 15
menu:
  preview:
    identifier: ysql-major-upgrade
    parent: manage-upgrade-deployment
    weight: 706
type: docs
---

Upgrading YugabyteDB from a version based on PostgreSQL 11 (all versions prior to v2.25) to a version based on PostgreSQL 15 (v2.25.1 or later) requires some additional steps.

## Before you begin

{{< warning >}}
v2.25 is a preview release that is only meant for evaluation purposes and should not be used in production.
{{< /warning >}}

- DDL statements are blocked for the duration of the upgrade. Consider executing all DDLs before the upgrade, and pause any jobs that might run DDLs.
- Upgrade client drivers.

    Upgrade all application client drivers to the new version. The client drivers are backwards compatible, and work with both the old and new versions of the database.
- Your cluster must be running v2024.2.2.0 or later.

    Deploy a new YugabyteDB cluster on version v2024.2.2.0 or later. If you have a pre-existing cluster, first upgrade it to the latest version in the v2024.2 series using the [upgrade instructions](../upgrade-deployment/).

### Precheck

New PostgreSQL major versions add many new features and performance improvements, but also remove some older unsupported features and data types. You can only upgrade after you remove all deprecated features and data types from your databases.

Use the [pg_upgrade](https://www.postgresql.org/docs/15/pgupgrade.html) tool provided with v2.25.1.0 to make sure your cluster is compatible with v2.25.1.0.

Use the `--check` option as follows:

```sh
pg_upgrade --check -U <superuser_name> --old-host <any_yb_node_address>  --old-port <port> --old-datadir <yb_data_dir>
```

For example:

```sh
$ ./yugabyte-2.25.1.0/postgres/bin/pg_upgrade --check -U yugabyte --old-host 127.0.0.1 --old-port 5433 --old-datadir ~/yugabyte-data/node-1/disk-1/pg_data_11
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

## Prepare the cluster for YSQL major upgrade

Since the upgrade is fully online, your cluster will temporarily consist of a mix of nodes running both PostgreSQL 11 and 15 ("mixed mode"). These processes need to be able to talk with each other and correctly process your SQL commands. Specifically, the PostgreSQL expressions that are used in the SQL statements get pushed down from the compute layer to the YugabyteDB storage layer. Because the expressions used by PostgreSQL have changed across the major version, you need to disable this optimization during the upgrade. (This will be addressed in the v2025.1 ({{<issue 24730>}}).)

Set the `ysql_yb_major_version_upgrade_compatibility` flag to `11` on all YB-Master and YB-TServer processes. For example:

```sh
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:7100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:7100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:7100 ysql_yb_major_version_upgrade_compatibility 11

$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:9100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:9100 ysql_yb_major_version_upgrade_compatibility 11
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:9100 ysql_yb_major_version_upgrade_compatibility 11
```

It is recommended to back up the cluster at this time. Refer to [Distributed snapshots for YSQL](../backup-restore/snapshot-ysql/).

## Upgrade phase

### Upgrade YB-Masters

[Upgrade the YB-Master](../upgrade-deployment/#3-upgrade-yb-masters) processes one at a time.

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

### Upgrade all YB-Tservers

[Upgrade the YB-Tserver processes](../upgrade-deployment/#4-upgrade-yb-tservers) one at a time.

Closely monitor your applications at this time. If any issues arise, you can [roll back](#rollback-phase) to the previous version. You can then address the issue and then retry the upgrade.

## Monitor phase

After all the YB-Master and YB-TServer processes are upgraded, monitor the cluster to ensure it is healthy. Make sure workloads are running as expected and there are no errors in the logs.

You can remain in this phase for as long as you need, but you should finalize the upgrade sooner rather than later to avoid operator errors that can arise from having to maintain two versions.

DDLs are not allowed even in this phase. New features that require format changes will not be available until the upgrade is finalized. Also, you cannot perform another upgrade until you have completed the current one.

If you are satisfied with the new version, proceed to [finalize the upgrade](#finalize-phase).

If you encounter any issues, you can [roll back](#rollback-phase) the upgrade.

## Finalize phase

Use yb-admin to finalize the upgrade.

You need to run the `finalize_upgrade` command as follows:

```sh
yb-admin --master_addresses <master_addresses> finalize_upgrade
```

## Rollback phase

### Roll back YB-TServers

[Roll back all YB-Tservers](../upgrade-deployment/#1-roll-back-yb-tservers).

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

### Roll back YB-Masters to v2024.2.2.0

[Roll back all YB-Masters](../upgrade-deployment/#2-roll-back-yb-masters) to v2024.2.2.0.

## Post upgrade

After the upgrade is done, you can re-enable expression pushdowns. For example:

```sh
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:7100 ysql_yb_major_version_upgrade_compatibility 0
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:7100 ysql_yb_major_version_upgrade_compatibility 0
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:7100 ysql_yb_major_version_upgrade_compatibility 0

$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.1:9100 ysql_yb_major_version_upgrade_compatibility 0
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.2:9100 ysql_yb_major_version_upgrade_compatibility 0
$ ./bin/yb-ts-cli set_flag --server_address 127.0.0.3:9100 ysql_yb_major_version_upgrade_compatibility 0
```

If you are using the cost based optimizer, run ANALYZE on your databases to get the most optimal query plans.

## Limitations

- YB-Tservers need to run on all YB-Master nodes. Kubernetes and Docker are not yet supported. {{<issue 24719>}}
- Temporary tables, refreshing materialized views, and expression pushdown are not available. {{<issue 24731>}}, {{<issue 24732>}}, {{<issue 24730>}}
- Upgrading with extensions is not supported. {{<issue 24733>}}
- If you are using the cost based optimizer, run ANALYZE after the upgrade. {{<issue 25721>}}
- Any backups that are taken in the monitoring phase can only be restored on a PG15 compatible universe (that is, backups cannot be restored if rollback is performed).

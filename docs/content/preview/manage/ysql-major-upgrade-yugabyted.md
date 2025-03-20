---
title: YSQL major upgrade
headerTitle: YSQL major upgrade
linkTitle: YSQL major upgrade
description: Upgrade YugabyteDB to PostgreSQL 15
headcontent: Upgrade YugabyteDB to a version that supports PG 15
menu:
  preview:
    identifier: ysql-major-upgrade-1
    parent: manage-upgrade-deployment
    weight: 706
type: docs
---

Upgrading YugabyteDB from a version based on PostgreSQL 11 (all versions prior to v2.25) to a version based on PostgreSQL 15 (v2.25.1 or later) requires additional steps. For instructions on upgrades within a major PostgreSQL version, refer to [Upgrade YugabyteDB](../upgrade-deployment/).

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-major-upgrade-yugabyted/" class="nav-link active">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>

  <li>
    <a href="../ysql-major-upgrade-local/" class="nav-link">
      <i class="icon-shell"></i>
      Local
    </a>
  </li>

</ul>

## Before you begin

{{< warning >}}
v2.25 is a preview release that is only meant for evaluation purposes and should not be used in production.
{{< /warning >}}

- All DDL statements, except ones related to Temporary table and Refresh Materialized View, are blocked for the duration of the upgrade. Consider executing all DDLs before the upgrade, and pause any jobs that might run DDLs.
- Upgrade client drivers.

    Upgrade all application client drivers to the new version. The client drivers are backwards compatible, and work with both the old and new versions of the database.
- Your cluster must be running v2024.2.2.0 or later.

    Deploy a new YugabyteDB cluster on version v2024.2.2.0 or later. If you have a pre-existing cluster, first upgrade it to the latest version in the v2024.2 series using the [upgrade instructions](../upgrade-deployment/).

### Precheck

New PostgreSQL major versions add many new features and performance improvements, but also remove some older unsupported features and data types. You can only upgrade after you remove all deprecated features and data types from your databases.

Use the `upgrade check_version_compatibility` command to make sure your cluster is compatible with the new version.

```sh
./path_to_new_version/bin/yugabyted upgrade check_version_compatibility \
    --base_dir=~/yugabyte-data/node1
```

```output
output: ✅ Clusters are compatible for upgrade.
```

## Upgrade phase

### Upgrade nodes

Restart the nodes one at a time as follows:

```sh
./path_to_new_version/bin/yugabyted stop --upgrade=true \
    --base_dir=~/yugabyte-data/node1 \
    --tserver_flags="ysql_yb_major_version_upgrade_compatibility=11" \
    --master_flags="ysql_yb_major_version_upgrade_compatibility=11"
```

```output
Stopped yugabyted using config /net/dev-server-hsunder/share/yugabyte-data/node1/conf/yugabyted.conf.
```

```sh
./path_to_new_version/bin/yugabyted start \
    --base_dir=~/yugabyte-data/node1
```

```output
Starting yugabyted...
✅ Upgrade status successfully verified   
✅ YugabyteDB Started                  
✅ Node joined a running cluster with UUID 5dd3bda3-43a9-48fd-9c16-8399378fed12
✅ UI ready         
✅ Data placement constraint successfully verified                 

+---------------------------------------------------------------------------------------------------+
|                                               yugabyted                                           |
+---------------------------------------------------------------------------------------------------+
| Status              : Running.                                                                    |
| YSQL Status         : Ready                                                                       |
| Replication Factor  : 3                                                                           |
| YugabyteDB UI       : http://127.0.0.1:15433                                                      |
| JDBC                : jdbc:postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte   |
| YSQL                : bin/ysqlsh -h 127.0.0.1  -U yugabyte -d yugabyte                            |
| YCQL                : bin/ycqlsh 127.0.0.1 9042 -u cassandra                                      |
| Data Dir            : /net/dev-server-hsunder/share/yugabyte-data/node1/data                      |
| Log Dir             : /net/dev-server-hsunder/share/yugabyte-data/node1/logs                      |
| Universe UUID       : 5dd3bda3-43a9-48fd-9c16-8399378fed12                                        |
+---------------------------------------------------------------------------------------------------+
```

### Upgrade YSQL catalog to the new version

Upgrade the YSQL catalog. This command can be run from any node.

```sh
./path_to_new_version/bin/yugabyted upgrade ysql_catalog \
    --base_dir=~/yugabyte-data1/node3
```

```output
✅ YSQL catalog upgrade successful.   

+----------------------------------------------------+
|                     yugabyted                      |
+----------------------------------------------------+
| Status        : YSQL catalog upgrade successful.   |
+----------------------------------------------------+
```

### Restart nodes

Restart the nodes again one at a time as follows:

```sh
./path_to_new_version/bin/yugabyted stop --upgrade=true \
    --base_dir=~/yugabyte-data/node1 \
    --tserver_flags="ysql_yb_major_version_upgrade_compatibility=0" \
    --master_flags="ysql_yb_major_version_upgrade_compatibility=0"
```

```output
Stopped yugabyted using config /net/dev-server-hsunder/share/yugabyte-data/node1/conf/yugabyted.conf.
```

```sh
./path_to_new_version/bin/yugabyted start \
    --base_dir=~/yugabyte-data/node1
```

```output
Starting yugabyted...
✅ Upgrade status successfully verified   
✅ YugabyteDB Started                  
✅ Node joined a running cluster with UUID 5dd3bda3-43a9-48fd-9c16-8399378fed12
✅ UI ready         
✅ Data placement constraint successfully verified                 

+---------------------------------------------------------------------------------------------------+
|                                               yugabyted                                           |
+---------------------------------------------------------------------------------------------------+
| Status              : Running.                                                                    |
| YSQL Status         : Ready                                                                       |
| Replication Factor  : 3                                                                           |
| YugabyteDB UI       : http://127.0.0.1:15433                                                      |
| JDBC                : jdbc:postgresql://127.0.0.1:5433/yugabyte?user=yugabyte&password=yugabyte   |
| YSQL                : bin/ysqlsh -h 127.0.0.1  -U yugabyte -d yugabyte                            |
| YCQL                : bin/ycqlsh 127.0.0.1 9042 -u cassandra                                      |
| Data Dir            : /net/dev-server-hsunder/share/yugabyte-data/node1/data                      |
| Log Dir             : /net/dev-server-hsunder/share/yugabyte-data/node1/logs                      |
| Universe UUID       : 5dd3bda3-43a9-48fd-9c16-8399378fed12                                        |
+---------------------------------------------------------------------------------------------------+
```

Closely monitor your applications at this time. If any issues arise, you can [roll back](#rollback-phase) to the previous version. You can then address the issue and then retry the upgrade.

## Monitor phase

After all the YB-Master and YB-TServer processes are upgraded, monitor the cluster to ensure it is healthy. Make sure workloads are running as expected and there are no errors in the logs.

You can remain in this phase for as long as you need, but you should finalize the upgrade sooner rather than later to avoid operator errors that can arise from having to maintain two versions.

DDLs are not allowed even in this phase. New features that require format changes will not be available until the upgrade is finalized. Also, you cannot perform another upgrade until you have completed the current one.

If you are satisfied with the new version, proceed to [finalize the upgrade](#finalize-phase).

## Finalize phase

After restarting all the nodes, finalize the upgrade by running the `yugabyted finalize_new_version` command. This command can be run from any node.

```sh
./path_to_new_version/bin/yugabyted upgrade finalize_new_version \
    --base_dir=~/yugabyte-data/node1 \
```

## Rollback phase

yugabyted does not currently support rollback.

## Post upgrade phase

- If you are using the cost based optimizer, run ANALYZE after the upgrade. {{<issue 25721>}}

## Limitations

- Expression pushdown is not available. {{<issue 24730>}}
- Upgrading with extensions is not yet supported. {{<issue 24733>}}
- Any backups that are taken in the monitoring phase can only be restored on a PG15 compatible universe (that is, backups cannot be restored if rollback is performed).

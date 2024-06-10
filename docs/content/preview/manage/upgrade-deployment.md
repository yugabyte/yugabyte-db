---
title: Upgrade YugabyteDB
headerTitle: Upgrade YugabyteDB
linkTitle: Upgrade YugabyteDB
description: Upgrade YugabyteDB
headcontent: Upgrade an existing YugabyteDB deployment
menu:
  preview:
    identifier: manage-upgrade-deployment
    parent: manage
    weight: 706
type: docs
---

{{< tip title="Tip" >}}
Ensure that you are using the most up-to-date version of the software to optimize performance, access new features, and safeguard against software bugs.
{{< /tip >}}

YugabyteDB is a distributed database that can be installed on multiple nodes. Upgrades happen in-place with minimal impact on availability and performance. This is achieved using a rolling upgrade process, where each node/process is upgraded one node at a time. YugabyteDB [automatically rebalances](../../explore/linear-scalability/data-distribution/) the cluster as nodes/processes are taken down and brought back up during the upgrade.

The `data`, `log`, and `conf` directories are typically stored in a fixed location that remains unchanged during the upgrade process. This ensures that the cluster's data and configuration settings are preserved throughout the upgrade.

## Important information

{{< warning >}}
Review the following information before starting an upgrade.
{{< /warning >}}

- Make sure your operating system is up to date. If your universe is running on a [deprecated OS](../../reference/configuration/operating-systems/), you need to update your OS before you can upgrade to the next major YugabyteDB release.

- You can only upgrade to the latest minor version of every release.

    For example, if you are upgrading from v2.18.3.0, and the latest release in the v2.20 release series is v2.20.2.0, then you must upgrade to v2.20.2.0 (and not v2.20.1.0 or v2.20.0.0).

    To view and download releases, refer to [Releases](../../releases/).

- Upgrades are not supported between preview and stable versions.

- Make sure you are following the instructions for the version of YugabyteDB that you are upgrading from. You can select the doc version using the version selector in the upper right corner of the page.

- Roll back is {{<badge/ea>}} and supported in v2.20.2 and later only. If you are upgrading from v2.20.1.x or earlier, follow the instructions for [v2.18](/v2.18/manage/upgrade-deployment/).

## Upgrade YugabyteDB cluster

You upgrade a cluster in the following phases:

- [Upgrade](#upgrade-phase)
- [Monitor](#monitor-phase)
- [Finalize](#a-finalize-phase) or [Rollback](#b-rollback-phase)

During the upgrade phase, you deploy the binaries of the new version to the YugabyteDB processes. Most of the incoming changes and bug fixes take effect at this stage. Some features, however, require changes to the format of data sent over the network, or stored on disk. These are not enabled until you finalize the upgrade. This allows you to evaluate the majority of the changes before committing to the new version. If you encounter any issues while monitoring the cluster, you have the option to roll back in-place and restore the cluster to its state before the upgrade.

### Upgrade Phase

#### 1. Prepare the cluster for the upgrade

Before starting the upgrade process, ensure that the cluster is healthy.

1. Make sure that all YB-Master processes are running at `http://<any-yb-master>:7000/`.
1. Make sure there are no leaderless or under replicated tablets at `http://<any-yb-master>:7000/tablet-replication`.
1. Make sure that all YB-TServer processes are running and the cluster load is balanced at `http://<any-yb-master>:7000/tablet-servers`.

![Tablet Servers](/images/manage/upgrade-deployment/tablet-servers.png)

{{< note title="Note" >}}
If upgrading a cluster that is in production and serving user traffic, it is recommended to take a [Backup](../../manage/backup-restore/) of all the data before starting the upgrade. In the unlikely event of a failure, you can restore from the backup to quickly regain access to the data.
{{< /note >}}

#### 2. Download and install the new version

Install the new version of YugabyteDB in a new directory on every YugabyteDB node. Follow the instructions in [Install Software](../../deploy/manual-deployment/install-software/).

For example:

```sh
wget https://downloads.yugabyte.com/yugabyte-$NEW_VER.tar.gz
tar xf yugabyte-$NEW_VER.tar.gz -C /home/yugabyte/softwareyb-$NEW_VER/
cd /home/yugabyte/softwareyb-$NEW_VER/
./bin/post_install.sh
```

If you are using PostgreSQL extensions, make sure to install the extensions in the new YugabyteDB version. Follow the instructions in [Install PostgreSQL extensions](../../explore/ysql-language-features/pg-extensions/install-extensions/).

#### 3. Upgrade YB-Masters

Upgrade the YB-Masters one node at a time:

1. Stop the `yb-master` process.

    ```sh
    pkill yb-master
    ```

1. Verify that you are in the directory of the new version.

    ```sh
    cd /home/yugabyte/softwareyb-$NEW_VER/
    ```

1. Start the new version of the YB-Master process. Follow the instructions in [Start YB-Masters](../../deploy/manual-deployment/start-masters/).

1. Make sure that all YB-Master processes are running at `http://<any-yb-master>:7000/`. If anything looks unhealthy, you can jump ahead to [Rollback Phase](#b-rollback-phase).

1. Pause for at least 60 seconds before upgrading the next YB-Master.

#### 4. Upgrade YB-TServers

Upgrade the YB-TServers one node at a time:

1. Stop the `yb-tserver` process.

    ```sh
    pkill yb-tserver
    ```

1. Verify that you're in the directory of the new version.

    ```sh
    cd /home/yugabyte/softwareyb-$NEW_VER/
    ```

1. Start the new version of the YB-TServer process. Follow the instructions in [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

1. Make sure that all YB-TServer processes are running at `http://<any-yb-master>:7000/tablet-servers`, and wait for the cluster load to balance. If anything looks unhealthy, you can jump ahead to [Rollback Phase](#b-rollback-phase).

1. Pause for at least 60 seconds before upgrading the next YB-TServer.

### Monitor Phase

Once all the YB-Master and YB-TServer processes have been upgraded, monitor the cluster to ensure it is healthy. Make sure workloads are running as expected and there are no errors in the logs.

You can remain in this phase for as long as you need, but it is recommended to finalize the upgrade sooner in order to avoid operator errors that can arise from having to maintain two versions. New features that require format changes will not be available until the upgrade is finalized. Also, you cannot perform another upgrade until you have completed the current one.

If you are satisfied with the new version, proceed to the [Finalize Phase](#a-finalize-phase). If you encounter any issues, you can proceed to [Rollback Phase](#b-rollback-phase).

### A. Finalize Phase

#### 1. Promote AutoFlags

New YugabyteDB features may require changes to the format of data that is sent over the network or stored on disk. During the upgrade process, these new features are kept disabled to prevent sending the new data formats to processes that are still running the old version. After all YugabyteDB processes have been upgraded, these features can be safely enabled. [AutoFlags](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md) helps simplify this procedure so that you don't have to worry about identifying what these features are and how to enable them. All new features are enabled using a single command.

1. Use the [yb-admin](../../admin/yb-admin/) utility to promote the new AutoFlags:

    ```sh
    ./bin/yb-admin \
        -master_addresses <master-addresses> \
        promote_auto_flags
    ```

    Expect to see the following output:

    ```output
    PromoteAutoFlags completed successfully
    New AutoFlags were promoted
    New config version: 2
    ```

    Or

    ```output
    PromoteAutoFlags completed successfully
    No new AutoFlags eligible to promote
    Current config version: 1
    ```

    {{< note title="Note" >}}

- `promote_auto_flags` is idempotent and can be run multiple times.
- Before promoting AutoFlags, ensure that all YugabyteDB processes in the cluster have been upgraded to the new version. Process running an old version may fail to connect to the cluster after the AutoFlags have been promoted.
    {{< /note >}}

1. Wait at least 10 seconds (`FLAGS_auto_flags_apply_delay_ms`) for the new AutoFlags to be propagated and applied on all YugabyteDB processes.

#### 2. Upgrade the YSQL system catalog

If you do not have [YSQL enabled](../../reference/configuration/yb-tserver/#ysql), you can skip this step.

Similar to PostgreSQL, YugabyteDB stores YSQL system metadata, referred to as the YSQL system catalog, in special tables. The metadata includes information about tables, columns, functions, users, and so on. This metadata is accessible through the YSQL API and is required for YSQL functionality.

When new YSQL features are added to YugabyteDB, objects such as tables, functions, and views may need to be added, updated, or deleted from the system catalog. If you create a new cluster using the latest release, it is initialized with the most recent pre-packaged YSQL system catalog snapshot. When upgrading, you need to manually upgrade the YSQL system catalog.

Use the [yb-admin](../../admin/yb-admin/) utility to upgrade the YSQL system catalog:

```sh
./bin/yb-admin \
    -master_addresses <master-addresses> \
    upgrade_ysql
```

Expect to see the following output:

```output
YSQL successfully upgraded to the latest version
```

In certain scenarios, a YSQL upgrade can take longer than 60 seconds, which is the default timeout value for `yb-admin`. If this happens, run the command with a greater `-timeout_ms` value. For example:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    -timeout_ms 180000 \
    upgrade_ysql
```

{{< note title="Note" >}}

- `upgrade_ysql` is idempotent and can be run multiple times.
- Concurrent YSQL operations in a cluster can lead to transactional conflicts, catalog version mismatches, and read restart errors. This is expected, and should be addressed by retrying the operation. If `upgrade_ysql` encounters these errors, then it should also be retried.
{{< /note >}}

### B. Rollback Phase

{{< warning title="Important" >}}

- Roll back is {{<badge/ea>}}.
- Roll back is only supported when you are upgrading a cluster that is already on version v2.20.2.0 or higher.
- You cannot roll back after finalizing the upgrade. If you still want to go back to the old version, you have to migrate your data to another cluster running the old version. You can either restore a backup taken while on the old version or [Export and import](../backup-restore/export-import-data/) the current data from the new version. The import script may have to be manually changed in order to conform to the query format of the old version.
{{< /warning >}}

In order to roll back to the version that you were on before the upgrade, you need to restart all YB-Master and YB-TServers on the old version. All YB-TServers have to be rolled back before you roll back YB-Masters.

#### 1. Roll back YB-TServers

Roll back the YB-TServers one node at a time:

1. Stop the `yb-tserver` process.

    ```sh
    pkill yb-tserver
    ```

1. Verify that you're in the directory of the old version.

    ```sh
    cd /home/yugabyte/softwareyb-$OLD_VER/
    ```

1. Start the old version of the YB-TServer process. Follow the instructions in [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

1. Make sure that all YB-TServer processes are running and the cluster load is balanced at `http://<any-yb-master>:7000/tablet-servers`.

1. Pause for at least 60 seconds before rolling back the next YB-TServer.

#### 2. Roll back YB-Masters

Use the following procedure to roll back all YB-Masters:

1. Stop the `yb-master` process.

    ```sh
    pkill yb-master
    ```

1. Verify that you are in the directory of the old version.

    ```sh
    cd /home/yugabyte/softwareyb-$OLD_VER/
    ```

1. Start the old version of the YB-Master process. Follow the instructions in [Start YB-Masters](../../deploy/manual-deployment/start-masters/).

1. Make sure that all YB-Master processes are running at `http://<any-yb-master>:7000/`.

1. Pause for at least 60 seconds before rolling back the next YB-Master.

## Upgrades with xCluster

When you have unidirectional xCluster replication, it is recommended to upgrade the target cluster before the source. After the target cluster is upgraded and finalized, you can proceed to upgrade the source cluster.

If you have bidirectional xCluster replication, then you should upgrade and finalize both clusters at the same time. Perform the upgrade steps for each cluster individually and monitor both of them. If you encounter any issues, roll back both clusters. If everything appears to be in good condition, finalize both clusters with as little delay as possible.

{{< note title="Note" >}}
xCluster replication requires the target cluster version to the same or later than the source cluster version. The setup of a new xCluster replication will fail if this check fails. Existing replications will automatically pause if the source cluster is finalized before the target cluster.
{{< /note >}}

## Advanced - enable volatile AutoFlags during monitoring

{{< warning title="Important" >}}
The instructions in the previous sections are sufficient for most users. The following instructions are for advanced users who want more coverage of new features during the monitoring phase. It involves a few more steps and can be error prone if not performed correctly.
{{< /warning >}}

During the standard Monitor phase, all new AutoFlags are kept in the default non-promoted state. Most new features that have data format changes only affect the data that is sent over the network between processes belonging to the same cluster. This data is in-memory and is never stored on disk. For the highest possible level of coverage of the incoming changes, you can enable these features during the monitoring phase. In the case of a rollback, they can be safely disabled. These features are guarded with a `kLocalVolatile` class AutoFlag.

During the Monitor phase, do the following:

1. Enable the volatile AutoFlags after the upgrade phase has completed:

    ```sh
    ./bin/yb-admin \
        -master_addresses <master-addresses> \
        promote_auto_flags kLocalVolatile
    ```

1. Copy the output and store it in a safe place.

1. Wait at least 10 seconds (`FLAGS_auto_flags_apply_delay_ms`) for the new AutoFlags to be propagated and applied on all YugabyteDB processes.

### Roll back volatile AutoFlags

If you need to roll back an upgrade where volatile AutoFlags were enabled, depending on the output of `promote_auto_flags`, you will need to *first roll back the AutoFlags that were promoted* before proceeding with the rollback phase.

- If the output of `promote_auto_flags kLocalVolatile` contained the following message:

    ```output
    No new AutoFlags eligible to promote
    ```

    You don't need to roll back the AutoFlags and you can proceed immediately with the regular rollback procedure.

- If the output of `promote_auto_flags kLocalVolatile` contained the following message:

    ```output
    New AutoFlags were promoted
    New config version: <new_config_version>
    ```

    Then you need to roll back the AutoFlags to the previous version as follows:

    ```sh
    ./bin/yb-admin \
        -master_addresses <master-addresses> \
        rollback_auto_flags <previous_config_version>
    ```

    Where `previous_config_version` is `new_config_version` - 1.

    For example, if the output is as follows:

    ```output
    PromoteAutoFlags completed successfully
    New AutoFlags were promoted
    New config version: 3
    ```

    Enter the following command to roll back the flags:

    ```sh
    ./bin/yb-admin \
        -master_addresses ip1:7100,ip2:7100,ip3:7100 \
        rollback_auto_flags 2
    ```

    ```output
    RollbackAutoFlags completed successfully
    AutoFlags that were promoted after config version 2 were successfully rolled back
    New config version: 4
    ```

For the Finalize phase, no extra steps are required. The Finalize phase will promote *all* the AutoFlags and upgrade the YSQL system catalog.

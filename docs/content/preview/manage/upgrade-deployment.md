---
title: Upgrade YugabyteDB
headerTitle: Upgrade YugabyteDB
linkTitle: Upgrade YugabyteDB
description: Upgrade YugabyteDB
headcontent: Upgrade YugabyteDB on your deployment
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

YugabyteDB is a distributed database that can be installed on multiple nodes. You can upgrade your cluster in place without any loss of availability and with minimum impact on active workloads. This is achieved using a rolling upgrade process, where each node/process is upgraded one node at a time.

The `data`, `log`, and `conf` directories are typically stored in a fixed location that remains unchanged during the upgrade process. This ensures that the cluster's data and configuration settings are preserved throughout the upgrade.

{{< note title="Note" >}}
- Upgrades are not supported between preview and stable versions.

- Make sure you are following the instructions for the version that you are upgrading from. You can select the doc version from the upper right corner of the page.

- If you are upgrading from `2.20.0` and `2.20.1` follow the instructions for [2.18](../../../v2.18/manage/upgrade-deployment/).
{{< /note >}}

## Upgrade YugabyteDB cluster

{{< warning title="Important" >}}
- You can only upgrade to the latest minor version of every release. For example, if you are on version `2.18.3.0`, and there is already a `2.20.2.0` release, then you must upgrade to `2.20.2.0` and not `2.20.1.0` or `2.20.0.0`.
Please visit the [Releases](../../releases/) page to check and download the latest releases.
{{< /warning >}}

The [Upgrade Phase](#upgrade-phase) deploys the YugabyteDB processes on the new versions. Most of the incoming changes and bug fixes are enabled at this stage. Some features however, require changes to format of data sent over the network, or stored on disk. These are not enabled until the [Finalize Phase](#a-finalize-phase). This provides you the option to evaluate the majority of the changes before committing to the new version. If you encounter any usual behavior before this you can jump to the [Rollback Phase](#b-rollback-phase) to rollback the cluster. The rollback will put you back in the state you were in before the upgrade.

### Upgrade Phase

#### 1. Prepare the cluster for the upgrade
Before starting the upgrade process, ensure that the cluster is healthy.
1. Make sure that all YB-Master processes are running at `http://<any-yb-master>:7000/`.
2. Make sure there are no leaderless or under replicated tablets at `http://<any-yb-master>:7000/tablet-replication`.
3. Make sure that all YB-Tserver processes are running and the cluster load is balanced at `http://<any-yb-master>:7000/tablet-servers`.


![Tablet Servers](/images/manage/upgrade-deployment/tablet-servers.png)

{{< note title="Note" >}}
If upgrading a cluster that is in production and serving user traffic, it is recommended to take a [Backup](../../manage/backup-restore/) of all the data before starting the upgrade. In the unlikely event of a failure, you can restore from the backup to quickly regain access to your data.
{{< /note >}}

#### 2. Download and install the new version

Install the new version of YugabyteDB in a new directory on every YugabyteDB node. Follow the instructions from [Install Software](../../deploy/manual-deployment/install-software/).

**Example for CentOS**
```sh
wget https://downloads.yugabyte.com/yugabyte-$NEW_VER.tar.gz
tar xf yugabyte-$NEW_VER.tar.gz -C /home/yugabyte/softwareyb-$NEW_VER/
cd /home/yugabyte/softwareyb-$NEW_VER/
./bin/post_install.sh
```

If you are using PostgreSQL extensions, make sure to install the extensions in the new YugabyteDB version. Follow the instructions from [Installing extensions](../../explore/ysql-language-features/pg-extensions/#installing-extensions).

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

1. Start the new version of the YB-Master process. Follow the instructions from [Start YB-Masters](../../deploy/manual-deployment/start-masters/).

1. Make sure that all YB-Master processes are running at `http://<any-yb-master>:7000/`.

1. Pause for at least 60 seconds before upgrading the next YB-Master.

1. If you encounter any unusual behavior, jump ahead to [Rollback Phase](#b-rollback-phase) and follow the instructions to rollback to the previous version.

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

1. Start the new version of the YB-TServer process. Follow the instructions from [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

1. Make sure that all YB-Tserver processes are running at `http://<any-yb-master>:7000/tablet-servers`, and wait for the cluster load to balance.

1. Pause for at least 60 seconds before upgrading the next YB-TServer.

1. If you encounter any unusual behavior, jump ahead to [Rollback Phase](#b-rollback-phase) and follow the instructions to rollback to the previous version.

### Monitor Phase

Once all the YB-Master and YB-Tserver processes have been upgraded, monitor the cluster to ensure it is healthy. Make sure workloads are running as expected and there are no errors in the logs.

You can be in this phase for as long as you need, but it is recommended to finalize the upgrade sooner to avoid operator errors that can arise from having to maintain two versions. New features that require format changes to data on disk will not be available until the upgrade is finalized. Also, you cannot perform another upgrade until you have completed the current one.

If you are satisfied with the new version proceed to the [Finalize Phase](#a-finalize-phase). If you encounter any unusual behavior, jump ahead to [Rollback Phase](#b-rollback-phase) and follow the instructions to rollback to the previous version.

### A. Finalize Phase

#### 1. Promote AutoFlags
New YugabyteDB features may require changes to the format of data that is sent over the wire or stored on disk. During the upgrade process, these new features are disabled to prevent sending the new data formats to processes that are still running the old version. After all YugabyteDB processes have been upgraded, these features can be safely enabled. [AutoFlags](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md) helps simplify this procedure so that you don't have to worry about identifying what these features are, and how to enable them. All new features are enabled using a single command.

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
``````
OR
```output
PromoteAutoFlags completed successfully
No new AutoFlags eligible to promote
Current config version: 1
``````

{{< note title="Note" >}}
- `promote_auto_flags` is idempotent and can be run multiple times.
- Before promoting AutoFlags, ensure that all YugabyteDB processes in the cluster have been upgraded to the new version. If any process running an old version may fail to connect to the cluster after the AutoFlags have been promoted.
{{< /note >}}

2. Wait at least 10 seconds (`FLAGS_auto_flags_apply_delay_ms`) for the new AutoFlags to be propagated and applied on all YugabyteDB processes.

#### 2. Upgrade the YSQL system catalog
If you do not have [YSQL enabled](../../reference/configuration/yb-tserver/#ysql-flags), you can skip this step.

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

In certain scenarios, a YSQL upgrade can take longer than 60 seconds, which is the default timeout value for `yb-admin`. If this happens, run the command with a greater `-timeout_ms` value:

**Example**
```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    -timeout_ms 180000 \
    upgrade_ysql
```

{{< note title="Note" >}}
- `upgrade_ysql` is idempotent and can be run multiple times without any side effects.
- Concurrent operations in a cluster can lead to transactional conflicts, catalog version mismatches, and read restart errors. This is expected, and should be addressed by retrying the operation. If `upgrade_ysql` encounters these errors, then it should also be retried.
{{< /note >}}

### B. Rollback Phase

{{< note title="Note" >}}
- You cannot rollback after finalizing the upgrade.
- Rollback is only supported when you are upgrading a cluster that is already on version `2.20.2.0` or higher.
{{< /note >}}

In order to rollback to the version that you were on before the upgrade, you need to restart all YB-Master and YB-Tservers on the old version.

#### 1. Rollback YB-Tservers

Rollback the YB-Tservers one node at a time:

1. Stop the `yb-tserver` process.

    ```sh
    pkill yb-tserver
    ```

1. Verify that you're in the directory of the old version.

    ```sh
    cd /home/yugabyte/softwareyb-$OLD_VER/
    ```

1. Start the old version of the YB-TServer process. Follow the instructions from [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

1. Make sure that all YB-Tserver processes are running and the cluster load is balanced at `http://<any-yb-master>:7000/tablet-servers`.

1. Pause for at least 60 seconds before rolling back the next YB-TServer.

#### 2. Rollback YB-Masters

Use the following procedure to rollback all YB-Masters:

1. Stop the `yb-master` process.

    ```sh
    pkill yb-master
    ```

1. Verify that you are in the directory of the old version.

    ```sh
    cd /home/yugabyte/softwareyb-$OLD_VER/
    ```

1. Start the old version of the YB-Master process. Follow the instructions from [Start YB-Masters](../../deploy/manual-deployment/start-masters/).

1. Make sure that all YB-Master processes are running at http://<any-yb-master>:7000/.

1. Pause for at least 60 seconds before rolling back the next YB-Master.

## Upgrades with xCluster

When you have unidirectional xCluster replication, it is recommended to upgrade the target cluster before the source. Once the target cluster is upgraded and finalized, you can proceed to upgrade the source cluster.

If you have bidirectional xCluster replication, then you should upgrade and finalize both clusters at the same time. Run the [Upgrade Phase](#upgrade-phase) for each cluster individually and monitor both of them. If you encounter any unusual behavior, rollback both clusters. If everything looks healthy, finalize both with as little delay as possible.

{{< note title="Note" >}}
xCluster replication requires the target cluster version to be equal to or higher than the source cluster version. The setup of a new xCluster replication will fail if this check fails. Existing replications will automatically pause if the source is finalized before the target.
{{< /note >}}

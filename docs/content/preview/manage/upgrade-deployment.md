---
title: Upgrade a deployment
headerTitle: Upgrade a deployment
linkTitle: Upgrade a deployment
description: Upgrade a deployment
headcontent: Upgrade YugabyteDB on your deployment
menu:
  preview:
    identifier: manage-upgrade-deployment
    parent: manage
    weight: 706
type: docs
---

{{< tip title="Tip" >}}
Make sure that you are on the latest version of the software so that you get the best performance and the new features.
{{< /tip >}}

YugabyteDB is a distributed database that can be installed on multiple nodes. You can upgrade your cluster in-place without any loss of availability and minimum impact to the workloads. This is achieved using a rolling upgrade process, where each node\process is upgraded one at a time.

Your `data/log/conf` directories are generally stored in a separate location which stays the same across the upgrade so that the cluster data, its configuration settings are retained across the upgrade.

## Upgrade YugabyteDB

{{< warning title="Important" >}}
You can only upgrade to the latest minor version of every release. For example if you are on version 2.18.3, and there is already a 2.20.2 release, then you must go to 2.20.2 and not 2.20.1 or 2.20.0.

You can check and download the latest version of YugabyteDB from the [releases overview](../../releases/) page.
{{< /warning >}}

When upgrading to versions above `2.20.2` you have the ability to Rollback the upgrade.

{{< note title="Note" >}}
- Upgrades are not supported between preview and stable versions.

- Make sure you are following the instructions for the version that you are upgrading from. You can select the doc version from the upper right corner of the page.

- If you are upgrading from `2.20.0` and `2.20.1` follow the instructions for [2.18](../../../v2.18/manage/upgrade-deployment/).

- Rollback is only supported when you are upgrading a cluster that is already on version `2.20.2` or higher.
{{< /note >}}

### Upgrade Phase

#### 1. Prepare the cluster for upgrade
Before starting the upgrade process, ensure that the cluster is healthy.
1. Make sure that all YB-Master processes are running in `http://<any-yb-master>:7000/`.
2. Make sure there are no leaderless tablets or under replicated in `http://<any-yb-master>:7000/tablet-replication`.
3. Make sure that all YB-Tserver processes are running and the cluster load is balanced in `http://<any-yb-master>:7000/tablet-servers`.
![Tablet Servers](/images/manage/upgrade-deployment/tablet-servers.png)

{{< note title="Note" >}}
If upgrading a cluster that is in production and serving user traffic it is recommended to take a [Backup](../../manage/backup-restore/) of all the data before starting the upgrade. In the unlikely event of a failure, you can restore from the backup onto a new cluster.
{{< /note >}}

#### 2. Download and install the new version

Install the new version of YugabyteDB in a new location on every YugabyteDB node. Follow the instructions from [Install Software](../../deploy/manual-deployment/install-software/).

For CentOS, this would use commands similar to the following:
```sh
wget https://downloads.yugabyte.com/yugabyte-$NEW_VER.tar.gz
tar xf yugabyte-$NEW_VER.tar.gz -C /home/yugabyte/softwareyb-$NEW_VER/
cd /home/yugabyte/softwareyb-$NEW_VER/
./bin/post_install.sh
```

If you are using PostgreSQL extensions, make sure to install the extensions in the new YugabyteDB version before upgrading the servers. For more information, see [Installing extensions](../../explore/ysql-language-features/pg-extensions/#installing-extensions).

#### 3. Upgrade YB-Masters

Use the following procedure to upgrade all YB-Masters:

1. Stop the older version of the YB-Master process.

    ```sh
    pkill yb-master
    ```

1. Verify that you are on the directory of the new version.

    ```sh
    cd /home/yugabyte/softwareyb-$NEW_VER/
    ```

1. Start the newer version of the YB-Master process. Follow the instructions from [Start YB-Masters](../../deploy/manual-deployment/start-masters/).

1. Verify in `http://<any-yb-master>:7000/` that all YB-Masters are alive.

1. Pause for at least 60 seconds before upgrading the next YB-Master. If you encounter any issues, you can jump ahead to [Rollback Phase](#b-rollback-phase) to go back to the previous version.

#### 4. Upgrade YB-TServers

Use the following procedure to upgrade all YB-TServers:

1. Stop the older version of the yb-tserver process.

    ```sh
    pkill yb-tserver
    ```

1. Verify that you're on the directory of the new version.

    ```sh
    cd /home/yugabyte/softwareyb-$NEW_VER/
    ```

1. Start the newer version of the YB-TServer process. Follow the instructions from [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

1. Verify in `http://<any-yb-master>:7000/tablet-servers` that all YB-TServers are alive and heart beating. If you encounter any issues, you can jump ahead to [Rollback Phase](#b-rollback-phase) to go back to the previous version.

1. Pause for at least 60 seconds before upgrading the next YB-TServer.

### Monitor Phase

Once all the YB-Master and YB-Tserver processes have been upgraded, monitor the cluster to ensure that it is healthy.

If you are satisfied with the new version proceed to the [Finalize Phase](#a-finalize-phase). If you encounter any issues, you can jump ahead to [Rollback Phase](#b-rollback-phase) to go back to the previous version.

### A. Finalize Phase

#### 1. Promote AutoFlags
New YugabyteDB features may require changes to the format of data that is sent over the wire or stored on disk. During the upgrade process, new features are disabled to prevent sending the new data formats to processes that are still on the older version. After all YugabyteDB processes have been upgraded, these features can be safely enabled. [AutoFlags](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md) help simplify this procedure so that you don't have to worry about identifying what these features are, and how to enable them. All new features are enabled using a single command .

1. Use the [yb-admin](../../admin/yb-admin/) utility to promote the new AutoFlags, as follows:

```sh
./bin/yb-admin \
    -master_addresses <master-addresses> \
    promote_auto_flags
```


**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    promote_auto_flags
```

If the operation is successful you will receive a output similar to the following:

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

2. Wait at least 10 seconds (`FLAGS_auto_flags_apply_delay_ms`) for the new AutoFlags to be propagated and applied on all processes.

{{< note title="Note" >}}
- `promote_auto_flags` is idempotent and can be run multiple times without any side effects.
- Before promoting AutoFlags, ensure that all YugabyteDB processes in the cluster have been upgraded to the new version. If any process running an older version may fail to connect to the cluster after the AutoFlags have been promoted.
{{< /note >}}

#### 2. Upgrade the YSQL system catalog
If you do not have [YSQL enabled](../../reference/configuration/yb-tserver/#ysql-flags), you can skip this step.

Similar to PostgreSQL, YugabyteDB stores YSQL system metadata, referred to as the YSQL system catalog, in special tables. The metadata includes information about tables, columns, functions, users, and so on. This metadata is accessible through the YSQL API and is required for YSQL functionality.

When new features are added to YugabyteDB, new objects such as tables and functions may need to be added to the system catalog. If you create a new cluster using the latest release, it is initialized with the most recent pre-packaged YSQL system catalog snapshot. When upgrading, you need to manually upgrade the YSQL system catalog.

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

In certain scenarios, a YSQL upgrade can take longer than 60 seconds, which is the default timeout value for `yb-admin`. If this happens, run the command with a greater timeout value:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    -timeout_ms 180000 \
    upgrade_ysql
```

{{< note title="Note" >}}
- `upgrade_ysql` is idempotent and can be run multiple times without any side effects.
- Concurrent operations in a cluster can lead to transactional conflicts, catalog version mismatches, and read restart errors. This is expected, and should be addressed by retrying the operation.
{{< /note >}}

### B. Rollback Phase

{{< note title="Note" >}}
You cannot rollback after finalizing the upgrade.
{{< /note >}}

You can rollback to the version that you were on before the upgrade.

#### 1. Rollback YB-Tservers

Use the following procedure to rollback all YB-TServers:

1. Stop the new version of the yb-tserver process.

    ```sh
    pkill yb-tserver
    ```

1. Verify that you're on the directory of the old version.

    ```sh
    cd /home/yugabyte/softwareyb-$OLD_VER/
    ```

1. Start the older version of the YB-TServer process. Follow the instructions from [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

1. Verify in `http://<any-yb-master>:7000/tablet-servers` that all YB-TServers are alive and heart beating.

1. Pause for at least 60 seconds before upgrading the next YB-TServer.

#### 2. Rollback YB-Masters

Use the following procedure to rollback all YB-Masters:

1. Stop the older version of the YB-Master process.

    ```sh
    pkill yb-master
    ```

1. Verify that you are on the directory of the old version.

    ```sh
    cd /home/yugabyte/softwareyb-$OLD_VER/
    ```

1. Start the older version of the YB-Master process. Follow the instructions from [Start YB-Masters](../../deploy/manual-deployment/start-masters/).

1. Verify in `http://<any-yb-master>:7000/` that all YB-Masters are alive.

1. Pause for at least 60 seconds before upgrading the next YB-Master.

## Upgrades with xCluster

When you have unidirectional xCluster replication it is recommended to first upgrade the target cluster before upgrading and source.

If you have bidirectional xCluster replication, then you should upgrade both clusters at the same time. Run the [Upgrade Phase](#upgrade-phase) for each cluster individually and Monitor both of them. If you encounter any issues, run the [Rollback Phase](#b-rollback-phase) on both clusters. Else, you can proceed to the [Finalize Phase](#a-finalize-phase) on both.

{{< note title="Note" >}}
xCluster is designed to automatically pause replication if the source of a replication is on a higher incompatible version compared to the target. Versions are incompatible if they have different `kExternal` AutoFlags.
{{< /note >}}

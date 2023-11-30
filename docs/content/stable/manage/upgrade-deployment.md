---
title: Upgrade a deployment
headerTitle: Upgrade a deployment
linkTitle: Upgrade a deployment
description: Upgrade a deployment
headcontent: Upgrade YugabyteDB on your deployment
menu:
  stable:
    identifier: manage-upgrade-deployment
    parent: manage
    weight: 706
type: docs
---

The basic flow is to upgrade each YB-Master and YB-TServer one at a time, verifying after each step from the YB-Master Admin UI that the cluster is healthy and the upgraded process is back online.

If you plan to script this in a loop, then a pause of approximately 60 seconds is recommended before moving from one process or node to another.

Your `data/log/conf` directories are generally stored in a separate location which stays the same across the upgrade so that the cluster data, its configuration settings are retained across the upgrade.

## Install new version of YugabyteDB

Install the new version of YugabyteDB in a new location. For CentOS, this would use commands similar to the following:

```sh
wget https://downloads.yugabyte.com/yugabyte-$VER.tar.gz
tar xf yugabyte-$VER.tar.gz -C /home/yugabyte/softwareyb-$VER/
cd /home/yugabyte/softwareyb-$VER/
./bin/post_install.sh
```

If you are using PostgreSQL extensions, make sure to install the extensions in the new YugabyteDB version before upgrading the servers. For more information, see [Installing extensions](../../explore/ysql-language-features/pg-extensions/#installing-extensions).

## Upgrade YB-Masters

Use the following procedure to upgrade a YB-Master:

1. Stop the older version of the YB-Master process, as follows:

    ```sh
    pkill yb-master
    ```

1. Verify that you are on the directory of the new version, as follows:

    ```sh
    cd /home/yugabyte/softwareyb-$VER/
    ```

1. Start the newer version of the YB-Master process. For more information, see [Start YB-Masters](../../deploy/manual-deployment/start-masters/).

1. Verify in `http://<any-yb-master>:7000/` that all YB-Masters are alive.

1. Pause for approximately 60 seconds before upgrading the next YB-Master.

## Upgrade YB-TServers

Use the following procedure to upgrade a YB-TServer:

1. Stop the older version of the yb-tserver process, as follows:

    ```sh
    pkill yb-tserver
    ```

1. Verify that you're on the directory of the new version, as follows:

    ```sh
    cd /home/yugabyte/softwareyb-$VER/
    ```

1. Start the newer version of the YB-TServer process. For more information, see [Start YB-TServers](../../deploy/manual-deployment/start-tservers/).

1. Verify in `http://<any-yb-master>:7000/tablet-servers` to see if the new YB-TServer is alive and heart beating.

1. Pause for approximately 60 seconds before upgrading the next YB-TServer.

## Promote AutoFlags

New YugabyteDB features may require changes to the format of data that is sent over the wire or stored on disk. During the upgrade process, these features have to be turned off to prevent sending the new data formats to nodes that are still running the older version. After all YugabyteDB processes have been upgraded to the new version, these features can be safely enabled.

[AutoFlags](https://github.com/yugabyte/yugabyte-db/blob/master/architecture/design/auto_flags.md) simplify this process so that you don't need to identify these features, find their corresponding flags, or determine what values to set them to. All new AutoFlags can be promoted to their desired target value using a single command.

Use the [yb-admin](../../admin/yb-admin/) utility to promote the new AutoFlags, as follows:

```sh
./bin/yb-admin \
    -master_addresses <master-addresses> \
    promote_auto_flags
```

The promotion of AutoFlags is an online operation that does not require stopping a running cluster or any process restarts. It is also an idempotent process, meaning it can be run multiple times without any side effects.

Note that it may take up to twice the value of `FLAGS_heartbeat_interval_ms` in milliseconds for the new AutoFlags to be fully propagated to all processes in the cluster.

{{< note title="Note" >}}
Before promoting AutoFlags, ensure that all YugabyteDB processes in the cluster have been upgraded to the new version. If any process running an older version attempts to connect to the cluster after the AutoFlags have been promoted, it may fail to do so.
{{< /note >}}

**Example**

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    promote_auto_flags
```

If the operation is successful you should see output similar to the following:

```output
PromoteAutoFlags status: 
New AutoFlags were promoted. Config version: 2
```

OR

```output
PromoteAutoFlags status: 
No new AutoFlags to promote
```

## Upgrade the YSQL system catalog

Similarly to PostgreSQL, YugabyteDB stores YSQL system metadata, referred to as the YSQL system catalog, in special tables. The metadata includes information about tables, columns, functions, users, and so on. The tables are stored separately, one for each database in the cluster.

When new features are added to YugabyteDB, objects such as new tables and functions need to be added to the system catalog. When you create a new cluster using the latest release, it is initialized with the most recent pre-packaged YSQL system catalog snapshot.

However, the YugabyteDB upgrade process only upgrades binaries, and doesn't affect the YSQL system catalog of an existing cluster - it remains in the same state as before the upgrade. To derive the benefits of the latest YSQL features when upgrading, you need to manually upgrade the YSQL system catalog.

The YSQL system catalog is accessible through the YSQL API and is required for YSQL functionality. YSQL system catalog upgrades are not required for clusters where [YSQL is not enabled](../../reference/configuration/yb-tserver/#ysql).

YSQL system catalog upgrades apply to clusters with YugabyteDB version 2.8 or later.

After completing the YugabyteDB upgrade process, use the [yb-admin](../../admin/yb-admin/) utility to upgrade the YSQL system catalog, as follows:

```sh
./bin/yb-admin \
    -master_addresses <master-addresses> \
    upgrade_ysql
```

Expect to see the following output:

```output
YSQL successfully upgraded to the latest version
```

In certain scenarios, a YSQL upgrade can take longer than 60 seconds, which is the default timeout value for `yb-admin`. If this happens, run the following command with a greater timeout value:

```sh
./bin/yb-admin \
    -master_addresses ip1:7100,ip2:7100,ip3:7100 \
    -timeout_ms 180000 \
    upgrade_ysql
```

Upgrading the YSQL system catalog is an online operation and does not require stopping a running cluster. `upgrade_ysql` is idempotent and can be run multiple times without any side effects.

Concurrent operations in a cluster can lead to transactional conflicts, catalog version mismatches, and read restart errors. This is expected, and should be addressed by rerunning `upgrade_ysql`.

## Upgrades and xCluster

When xCluster replication is configured, replication needs to be temporarily paused when upgrading any of the clusters involved in xCluster replication.

Use the following procedure to upgrade clusters involved in xCluster replication:

1. Pause xCluster replication on the clusters involved in replication. If the replication setup is bi-directional, ensure that replication is paused in both directions.

    ```sh
    ./bin/yb-admin \
        -master_addresses <master-addresses> \
        -certs_dir_name <cert_dir> \
        set_universe_replication_enabled <replication_group_name> 0
    ```

    Expect to see the following output:

    ```output
    Replication disabled successfully
    ```

2. Proceed to perform upgrade of all the clusters involved.
3. Resume replication on all the clusters involved using yb-admin.

    ```sh
    ./bin/yb-admin \
        -master_addresses <master-addresses> \
        -certs_dir_name <cert_dir> \
        set_universe_replication_enabled <replication_group_name> 1
    ```

    Expect to see the following output:

    ```output
    Replication enabled successfully
    ```

Downgrades are not currently supported. This is tracked in GitHub issue [#13686](https://github.com/yugabyte/yugabyte-db/issues/13686).

---
title: Upgrade a deployment
headerTitle: Upgrade a deployment
linkTitle: Upgrade a deployment
description: Upgrade a deployment
menu:
  v2.12:
    identifier: manage-upgrade-deployment
    parent: manage
    weight: 706
type: docs
---

The basic flow is to upgrade each YB-Master and YB-TServer one at a time, verifying after each step from the yb-master Admin UI that the cluster is healthy and the upgraded process is back online.

If you plan to script this in a loop, then a pause of about 60 seconds is recommended before moving from one process or node to another.

{{<tip title="Preserving data and cluster configuration across upgrades" >}}

Your `data/log/conf` directories are generally stored in a separate location which stays the same across the upgrade so that the cluster data, its configuration settings are retained across the upgrade.

{{< /tip >}}

## Install new version of YugabyteDB

Install the new version of YugabyteDB in a new location. For CentOS, this would be something like:

```sh
wget https://downloads.yugabyte.com/yugabyte-$VER.tar.gz
tar xf yugabyte-$VER.tar.gz -C /home/yugabyte/softwareyb-$VER/
cd /home/yugabyte/softwareyb-$VER/
./bin/post_install.sh
```

{{< note title="Note" >}}

If you are using PostgreSQL extensions, make sure to install the extensions in the new YugabyteDB version before upgrading. Follow the steps in [Install and use extensions](../../api/ysql/extensions).

{{< /note >}}

## Upgrade YB-Masters

- Stop the older version of the yb-master process.

```sh
pkill yb-master
```

- Verify that you're on the directory of the new version.

```sh
cd /home/yugabyte/softwareyb-$VER/
```

- Start the newer version of the yb-master process.

- Verify in `http://<any-yb-master>:7000/` that all masters are alive.

- Pause ~60 seconds before upgrading the next yb-master.

## Upgrade YB-TServers

- Stop the older version of the yb-tserver process.

```sh
pkill yb-tserver
```

- Verify that you're on the directory of the new version.

```sh
cd /home/yugabyte/softwareyb-$VER/
```

- Start the newer version of the yb-tserver process.

- Verify in `http://<any-yb-master>:7000/tablet-servers` to see if the new YB-TServer is alive and heart beating.

- Pause ~60 seconds before upgrading the next yb-tserver.

## Upgrade YSQL system catalog

Similar to PostgreSQL, YugabyteDB YSQL stores the system metadata, (also referred to as system catalog) which includes information about tables, columns, functions, users, and so on in special tables separately, for each database in the cluster.

YSQL system catalog comes as an additional layer to store metadata on top of YugabyteDB software itself. It is accessible through YSQL API and is crucial for the YSQL functionality.
YSQL system catalog upgrades are not required for clusters where YSQL is not enabled. Learn more about configuring YSQL flags [here](../../reference/configuration/yb-tserver/#ysql-flags).

{{< note title="Note" >}}
YSQL system catalog upgrades are applicable for clusters with YugabyteDB version 2.8 or higher.
{{< /note >}}

### Why upgrade YSQL system catalog

With the addition of new features , there's a need to add more objects such as new tables and functions to the YSQL system catalog.

The usual YugabyteDB upgrade process involves only upgrading binaries, and it doesn't affect YSQL system catalog of an existing cluster; it remains in the same state as it was before the upgrade.

While a newly created cluster on the latest release is initialized with the most recent pre packaged YSQL system catalog snapshot, an older cluster might want to manually upgrade YSQL system catalog to the latest state instead, thus getting all the benefits of the latest YSQL features.

### How to upgrade YSQL system catalog

After the YugabyteDB upgrade process completes successfully, use the [yb-admin](../../admin/yb-admin/) utility to perform an upgrade of the YSQL system catalog(YSQL upgrade) as follows:

```sh
$ ./bin/yb-admin upgrade_ysql
```

For a successful YSQL upgrade, a message will be displayed as follows:

```output
YSQL successfully upgraded to the latest version
```

In certain scenarios, a YSQL upgrade can take longer than 60 seconds, which is the default timeout value for `yb-admin`. To account for that, run the command with a higher timeout value:

```sh
$ ./bin/yb-admin -timeout_ms 180000 upgrade_ysql
```

Running the above command is an online operation and doesn't require stopping a running cluster. This command is idempotent and can be run multiple times without any side effects.

{{< note title="Note" >}}
Concurrent operations in a cluster can lead to various transactional conflicts, catalog version mismatches, and read restart errors. This is expected, and should be addressed by rerunning the upgrade command.
{{< /note >}}

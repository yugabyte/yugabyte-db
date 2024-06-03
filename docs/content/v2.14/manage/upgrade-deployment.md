---
title: Upgrade a deployment
headerTitle: Upgrade a deployment
linkTitle: Upgrade a deployment
description: Upgrade a deployment
menu:
  v2.14:
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

If you are using PostgreSQL extensions, make sure to install the extensions in the new YugabyteDB version before upgrading the servers. Follow the steps in [Installing extensions](../../explore/ysql-language-features/pg-extensions/#installing-extensions).

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

## Upgrade the YSQL system catalog

Similar to PostgreSQL, YugabyteDB stores YSQL system metadata, referred to as the YSQL system catalog, in special tables. The metadata includes information about tables, columns, functions, users, and so on. The tables are stored separately, one for each database in the cluster.

When new features are added to YugabyteDB, objects such as new tables and functions need to be added to the system catalog. When you create a new cluster using the latest release, it is initialized with the most recent pre-packaged YSQL system catalog snapshot.

However, the YugabyteDB upgrade process only upgrades binaries, and doesn't affect the YSQL system catalog of an existing cluster - it remains in the same state as before the upgrade. To derive the benefits of the latest YSQL features when upgrading, you need to manually upgrade the YSQL system catalog.

The YSQL system catalog is accessible through the YSQL API and is required for YSQL functionality. YSQL system catalog upgrades are not required for clusters where [YSQL is not enabled](../../reference/configuration/yb-tserver/#ysql-flags).

{{< note title="Note" >}}
YSQL system catalog upgrades apply to clusters with YugabyteDB version 2.8 or higher.
{{< /note >}}

### How to upgrade the YSQL system catalog

After completing the YugabyteDB upgrade process, use the [yb-admin](../../admin/yb-admin/) utility to upgrade the YSQL system catalog as follows:

```sh
$ ./bin/yb-admin upgrade_ysql
```

For a successful YSQL upgrade, you will see the following output:

```output
YSQL successfully upgraded to the latest version
```

In certain scenarios, a YSQL upgrade can take longer than 60 seconds, the default timeout value for `yb-admin`. If this happens, run the command with a higher timeout value:

```sh
$ ./bin/yb-admin -timeout_ms 180000 upgrade_ysql
```

Upgrading the YSQL system catalog is an online operation and doesn't require stopping a running cluster. `upgrade_ysql` is idempotent and can be run multiple times without any side effects.

{{< note title="Note" >}}
Concurrent operations in a cluster can lead to various transactional conflicts, catalog version mismatches, and read restart errors. This is expected, and should be addressed by re-running `upgrade_ysql`.
{{< /note >}}

---
title: Deploy to two universes with asynchronous replication
headerTitle: Asynchronous Replication
linkTitle: Asynchronous Replication
description: Enable deployment using unidirectional (master-follower) or bidirectional (multi-master) replication between universes
aliases:
  - /latest/deploy/multi-dc/2dc-deployment
menu:
  latest:
    parent: multi-dc
    identifier: async-replication
    weight: 633
type: page
isTocNested: true
showAsideToc: true
---

You can perform deployment using unidirectional (master-follower) or bidirectional (multi-master) asynchronous replication between universes (also known as data centers).

For information on two data center (2DC) deployment architecture and supported replication scenarios, see [Two data center (2DC) deployments](../../../architecture/docdb-replication/async-replication/).

## Setting Up Universes

You can create source and target universes as follows:

1. Create the yugabyte-source universe by following the procedure from [Manual deployment](../../manual-deployment).
1. Create tables for the APIs being used by the source universe.
1. Create the yugabyte-target universe by following the procedure from [Manual deployment](../../manual-deployment).
1. Create tables for the APIs being used by the target universe. These should be the same tables as you created for the source universe.
1. Proceed to setting up [unidirectional](#seting-up-unidirectional-replication) or [bidirectional](#setting-up-bidirectional-replication) replication.

## Setting Up Unidirectional Replication

After you created the required tables, you can set up asynchronous replication as follows:

- Look up the source universe UUID and the table IDs for the two tables and the index table:

    - To find a universe's UUID, check `/varz` for `--cluster_uuid`. If it is not available in this location, check the same field in the cluster configuration.

    - To find a table ID, execute the following command as an admin user:

      ```shell
      yb-admin list_tables include_table_id
      ```

- Run the following `yb-admin` [`setup_universe_replication`](../../../admin/yb-admin/#setup-universe-replication) command from the YugabyteDB home directory in the source universe:

    ```sh
    ./bin/yb-admin \
      -master_addresses <target_universe_master_addresses> \
      setup_universe_replication <source_universe_uuid> \
        <source_universe_master_addresses> \
        <table_id>,[<table_id>..]
    ```

    For example:

    ```sh
    ./bin/yb-admin \
      -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
      setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3 \
        127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
    ```


The preceding command contains three table IDs: the first two are YSQL for the base table and index, and the third is the YCQL table. 

Also, be sure to specify all master addresses for source and target universes in the command.

If you need to set up bidirectional replication, see instructions provided in [Setting Up Bidirectional Replication](#setting-up-bidirectional-replication). Otherwise, proceed to [Loading Data into the Source Universe](#loading-data-into-the-source-universe).

## Setting Up Bidirectional Replication

To set up bidirectional replication, repeat the procedure described in [Setting Up Unidirectional Replication](#setting-up-unidirectional-replication) applying the steps to the yugabyte-target universe. You need to set up each yugabyte-source to consume data from yugabyte-target.

When completed, proceed to [Loading Data](#loading-data-into-the-source-universe).

## Loading Data into the Source Universe

Once you have set up replication, load data into the source universe as follows:

- Download the YugabyteDB workload generator JAR file `yb-sample-apps.jar` from [GitHub](https://github.com/yugabyte/yb-sample-apps/releases).

- Start loading data into yugabyte-source by following examples for YSQL or YCQL:

  - YSQL:

  ```sh
  java -jar yb-sample-apps.jar --workload SqlSecondaryIndex --nodes 127.0.0.1:5433
  ```

  - YCQL:

  ```sh
  java -jar yb-sample-apps.jar --workload CassandraBatchKeyValue --nodes 127.0.0.1:9042
  ```

  Note that the IP address needs to correspond to the IP of any T-Servers in the cluster.

- For bidirectional replication, repeat the preceding step in the yugabyte-target universe.

When completed, proceed to [Verifying Replication](#verifying-replication).

## Verifying Replication

You can verify replication by stopping the workload and then using the `COUNT(*)` function on the yugabyte-target to yugabyte-source match.

### Unidirectional Replication

For unidirectional replication, connect to the yugabyte-target universe using the YSQL shell (`ysqlsh`) or the YCQL shell (`ycqlsh`), and confirm that you can see the expected records.

### Bidirectional Replication

For bidirectional replication, repeat the procedure described in [Unidirectional Replication](#unidirectional-replication), but reverse the source and destination information, as follows:

1. Run `yb-admin setup_universe_replication` on the yugabyte-target universe, pointing to yugabyte-source.
2. Use the workload generator to start loading data into the yugabyte-target universe.
3. Verify replication from yugabyte-target to yugabyte-source.

To avoid primary key conflict errors, keep the key ranges for the two universes separate. This is done automatically by the applications included in the `yb-sample-apps.jar`.

### Replication Lag

Replication lag is computed at the tablet level as follows:

`replication lag = hybrid_clock_time - last_read_hybrid_time`

*hybrid_clock_time* is the hybrid clock timestamp on the source's tablet-server, and *last_read_hybrid_time* is the hybrid clock timestamp of the latest record pulled from the source.

An example script [`determine_replication_lag.sh`](/files/determine_replication_lag.sh) calculates the replication lag. The script requires the [`jq`](https://stedolan.github.io/jq/) package.

The following example generates a replication lag summary for all tables on a cluster. You can also request an individual table.

```sh
./determine_repl_latency.sh -m 10.150.255.114,10.150.255.115,10.150.255.113
```

To obtain a summary of all command options, execute `determine_repl_latency.sh -h` .

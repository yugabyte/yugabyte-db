---
title: Deploy to two universes with xCluster replication
headerTitle: xCluster deployment
linkTitle: xCluster
description: Enable deployment using unidirectional (master-follower) or bidirectional (multi-master) replication between universes
headContent: Unidirectional (master-follower) and bidirectional (multi-master) replication
menu:
  stable:
    parent: async-replication
    identifier: async-deployment
    weight: 10
type: docs
---

## Set up universes

You can create source and target universes as follows:

1. Create the source universe by following the procedure from [Manual deployment](../../../manual-deployment/).
1. Create tables for the APIs being used by the source universe.
1. Create the target universe by following the procedure from [Manual deployment](../../../manual-deployment/).
1. Create tables for the APIs being used by the target universe. These should be the same tables as you created for the source universe.
1. Proceed to setting up [unidirectional](#set-up-unidirectional-replication) or [bidirectional](#set-up-bidirectional-replication) replication.

If you already have existing data in your tables, follow the bootstrap process described in [Bootstrap a target universe](#bootstrap-a-target-universe).

## Set up unidirectional replication

After you created the required tables, you can set up unidirectional replication as follows:

- Look up the source universe UUID and the table IDs for the two tables and the index table:

  - To find a universe's UUID, check `http://yb-master-ip:7000/varz` for `--cluster_uuid`. If it is not available in this location, check the same field in the universe configuration.

  - To find a table ID, execute the following command as an admin user:

      ```sh
      ./bin/yb-admin -master_addresses <source_universe_master_addresses> list_tables include_table_id
      ```

      The preceding command lists all the tables, including system tables. To locate a specific table, you can add `grep`, as follows:

      ```sh
      ./bin/yb-admin -master_addresses <source_universe_master_addresses> list_tables include_table_id | grep table_name
      ```

- Run the following `yb-admin` [`setup_universe_replication`](../../../../admin/yb-admin/#setup-universe-replication) command from the YugabyteDB home directory in the source universe:

    ```sh
    ./bin/yb-admin \
      -master_addresses <target_universe_master_addresses> \
      setup_universe_replication <source_universe_UUID>_<replication_stream_name> \
        <source_universe_master_addresses> \
        <table_id>,[<table_id>..]
    ```

    For example:

    ```sh
    ./bin/yb-admin \
      -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
      setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3_xClusterSetup1 \
        127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
        000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
    ```

The preceding command contains three table IDs: the first two are YSQL for the base table and index, and the third is the YCQL table. Make sure that all master addresses for source and target universes are specified in the command.

If you need to set up bidirectional replication, see instructions provided in [Set up bidirectional replication](#set-up-bidirectional-replication). Otherwise, proceed to [Load data into the source universe](#load-data-into-the-source-universe).

## Set up bidirectional replication

To set up bidirectional replication, repeat the procedure described in [Set up unidirectional replication](#set-up-unidirectional-replication) applying the steps to the target universe. You need to set up each source to consume data from target.

When completed, proceed to [Load data](#load-data-into-the-source-universe).

## Load data into the source universe

After you have set up replication, load data into the source universe, as follows:

- Download the YugabyteDB workload generator JAR file `yb-sample-apps.jar` from [GitHub](https://github.com/yugabyte/yb-sample-apps/releases).

- Start loading data into source by following examples for YSQL or YCQL:

  - YSQL:

    ```sh
    java -jar yb-sample-apps.jar --workload SqlSecondaryIndex --nodes 127.0.0.1:5433
    ```

  - YCQL:

    ```sh
    java -jar yb-sample-apps.jar --workload CassandraBatchKeyValue --nodes 127.0.0.1:9042
    ```

  Note that the IP address needs to correspond to the IP of any YB-TServers in the universe.

- For bidirectional replication, repeat the preceding step in the target universe.

When completed, proceed to [Verify replication](#verify-replication).

## Verify replication

You can verify replication by stopping the workload and then using the `COUNT(*)` function on the target to source match.

### Unidirectional replication

For unidirectional replication, connect to the target universe using the YSQL shell (`ysqlsh`) or the YCQL shell (`ycqlsh`), and confirm that you can see the expected records.

### Bidirectional replication

For bidirectional replication, repeat the procedure described in [Unidirectional replication](#unidirectional-replication), but reverse the source and destination information, as follows:

1. Run `yb-admin setup_universe_replication` on the target universe, pointing to source.
2. Use the workload generator to start loading data into the target universe.
3. Verify replication from target to source.

To avoid primary key conflict errors, keep the key ranges for the two universes separate. This is done automatically by the applications included in the `yb-sample-apps.jar`.

### Replication lag

Replication lag is computed at the tablet level as follows:

`replication lag = hybrid_clock_time - last_read_hybrid_time`

*hybrid_clock_time* is the hybrid clock timestamp on the source's tablet server, and *last_read_hybrid_time* is the hybrid clock timestamp of the latest record pulled from the source.

To obtain information about the overall maximum lag, you should check `/metrics` or `/prometheus-metrics` for `async_replication_sent_lag_micros` or `async_replication_committed_lag_micros` and take the maximum of these values across each source's YB-TServer. For information on how to set up the Node Exporter and Prometheus manually, see [Prometheus integration](../../../../explore/observability/prometheus-integration/macos/).

### Replication status

You can use `yb-admin` to return the current replication status. The `get_replication_status` command returns the replication status for all *consumer-side* replication streams. An empty `errors` field means the replication stream is healthy.

```sh
./bin/yb-admin \
    -master_addresses 127.0.0.1:7000,127.0.0.2:7000,127.0.0.3:7000 \
    get_replication_status
```

```output.yaml
statuses {
  table_id: "03ee1455f2134d5b914dd499ccad4377"
  stream_id: "53441ad2dd9f4e44a76dccab74d0a2ac"
  errors {
    error: REPLICATION_MISSING_OP_ID
    error_detail: "Unable to find expected op id on the producer"
  }
}
```

## Set up replication with TLS

The setup process depends on whether the source and target universes have the same certificates.

If both universes use the same certificates, run `yb-admin setup_universe_replication` and include the [`-certs_dir_name`](../../../../admin/yb-admin#syntax) flag. Setting that to the target universe's certificate directory will make replication use those certificates for connecting to both universes.

Consider the following example:

```sh
./bin/yb-admin -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
  -certs_dir_name /home/yugabyte/yugabyte-tls-config \
  setup_universe_replication e260b8b6-e89f-4505-bb8e-b31f74aa29f3_xClusterSetup1 \
  127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
  000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
```

When universes use different certificates, you need to store the certificates for the source universe on the target universe, as follows:

1. Ensure that `use_node_to_node_encryption` is set to `true` on all [YB-Masters](../../../../reference/configuration/yb-master/#use-node-to-node-encryption) and [YB-TServers](../../../../reference/configuration/yb-tserver/#use-node-to-node-encryption) on both the source and target.

1. For each YB-Master and YB-TServer on the target universe, set the flag `certs_for_cdc_dir` to the parent directory where you want to store all the source universe's certificates for replication.

1. Find the certificate authority file used by the source universe (`ca.crt`). This should be stored in the [`--certs_dir`](../../../../reference/configuration/yb-master/#certs-dir).

1. Copy this file to each node on the target. It needs to be copied to a directory named`<certs_for_cdc_dir>/<source_universe_uuid>`.

    For example, if you previously set `certs_for_cdc_dir=/home/yugabyte/yugabyte_producer_certs`, and the source universe's ID is `00000000-1111-2222-3333-444444444444`, then you would need to copy the certificate file to `/home/yugabyte/yugabyte_producer_certs/00000000-1111-2222-3333-444444444444/ca.crt`.

1. Set up replication using `yb-admin setup_universe_replication`, making sure to also set the `-certs_dir_name` flag to the directory with the target universe's certificates (this should be different from the directory used in the previous steps).

    For example, if you have the target universe's certificates in `/home/yugabyte/yugabyte-tls-config`, then you would run the following:

    ```sh
    ./bin/yb-admin -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
      -certs_dir_name /home/yugabyte/yugabyte-tls-config \
      setup_universe_replication 00000000-1111-2222-3333-444444444444_xClusterSetup1 \
      127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
      000030a5000030008000000000004000,000030a5000030008000000000004005,dfef757c415c4b2cacc9315b8acb539a
    ```

## Set up replication with geo-partitioning

You start by creating a source and a target universe with the same configurations (the same regions and zones), as follows:

- Regions: EU(Paris), Asia Pacific(Mumbai), US West(Oregon)
- Zones: eu-west-3a, ap-south-1a, us-west-2a

  ```sh
  ./bin/yb-ctl --rf 3 create --placement_info "cloud1.region1.zone1,cloud2.region2.zone2,cloud3.region3.zone3"
  ```

  Consider the following example:

  ```sh
  ./bin/yb-ctl --rf 3 create --placement_info "aws.us-west-2.us-west-2a,aws.ap-south-1.ap-south-1a,aws.eu-west-3.eu-west-3a"
  ```

Create tables, tablespaces, and partition tables at both the source and target universes, as per the following example:

- Main table: transactions
- Tablespaces: eu_ts, ap_ts, us_ts
- Partition tables: transactions_eu, transactions_in, transactions_us

  ```sql
  CREATE TABLE transactions (
      user_id   INTEGER NOT NULL,
      account_id INTEGER NOT NULL,
      geo_partition VARCHAR,
      amount NUMERIC NOT NULL,
      created_at TIMESTAMP DEFAULT NOW()
  ) PARTITION BY LIST (geo_partition);

  CREATE TABLESPACE eu_ts WITH(
      replica_placement='{"num_replicas": 1, "placement_blocks":
      [{"cloud": "aws", "region": "eu-west-3","zone":"eu-west-3a", "min_num_replicas":1}]}');

  CREATE TABLESPACE us_ts WITH(
      replica_placement='{"num_replicas": 1, "placement_blocks":
      [{"cloud": "aws", "region": "us-west-2","zone":"us-west-2a", "min_num_replicas":1}]}');

  CREATE TABLESPACE ap_ts WITH(
      replica_placement='{"num_replicas": 1, "placement_blocks":
      [{"cloud": "aws", "region": "ap-south-1","zone":"ap-south-1a", "min_num_replicas":1}]}');

  CREATE TABLE transactions_eu
                    PARTITION OF transactions
                    (user_id, account_id, geo_partition, amount, created_at,
                    PRIMARY KEY (user_id HASH, account_id, geo_partition))
                    FOR VALUES IN ('EU') TABLESPACE eu_ts;

  CREATE TABLE transactions_in
                    PARTITION OF transactions
                    (user_id, account_id, geo_partition, amount, created_at,
                    PRIMARY KEY (user_id HASH, account_id, geo_partition))
                    FOR VALUES IN ('IN') TABLESPACE ap_ts;

  CREATE TABLE transactions_us
                    PARTITION OF transactions
                    (user_id, account_id, geo_partition, amount, created_at,
                    PRIMARY KEY (user_id HASH, account_id, geo_partition))
                    DEFAULT TABLESPACE us_ts;
  ```

To create unidirectional replication, perform the following:

1. Collect partition table UUIDs from the source universe (partition tables, transactions_eu, transactions_in, transactions_us) by navigating to **Tables** in the Admin UI available at 127.0.0.1:7000. These UUIDs are to be used while setting up replication.

    ![xCluster_with_GP](/images/explore/yb_xcluster_table_uuids.png)

1. Run the replication setup command for the source universe, as follows:

    ```sh
    ./bin/yb-admin -master_addresses <target_master_addresses> \
    setup_universe_replication <source_universe_UUID>_<replication_stream_name> \
    <source_master_addresses> <comma_separated_table_ids>
    ```

    Consider the following example:

    ```sh
    ./bin/yb-admin -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 \
    setup_universe_replication 00000000-1111-2222-3333-444444444444_xClusterSetup1 \
    127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    000033e1000030008000000000004007,000033e100003000800000000000400d,000033e1000030008000000000004013
    ```

1. Optionally, if you have access to YugabyteDB Anywhere, you can observe the replication setup (`xClusterSetup1`) by navigating to **Replication** on the source and target universe.

## Set up xCluster replication in Kubernetes

In the Kubernetes environment, you can set up a pod to pod connectivity, as follows:

- Create a source and a target universe.

- Create tables in both universes, as follows:

  - Execute the following commands for the source universe:

      ```sh
    kubectl exec -it -n <source_universe_namespace> -t <source_universe_master_leader> -c <source_universe_container> -- bash
      /home/yugabyte/bin/ysqlsh -h <source_universe_yqlserver>
      create table query
    ```

    Consider the following example:

      ```sh
    kubectl exec -it -n xcluster-source -t yb-master-2 -c yb-master -- bash
      /home/yugabyte/bin/ysqlsh -h yb-tserver-1.yb-tservers.xcluster-source
      create table employees(id int primary key, name text);
      ```

  - Execute the following commands for the target universe:

      ```sh
    kubectl exec -it -n <target_universe_namespace> -t <target_universe_master_leader> -c <target_universe_container> -- bash
      /home/yugabyte/bin/ysqlsh -h <target_universe_yqlserver>
      create table query
    ```

    Consider the following example:

      ```sh
    kubectl exec -it -n xcluster-target -t yb-master-2 -c yb-master -- bash
      /home/yugabyte/bin/ysqlsh -h yb-tserver-1.yb-tservers.xcluster-target
      create table employees(id int primary key, name text);
      ```

- Collect table UUIDs by navigating to **Tables** in the Admin UI available at 127.0.0.1:7000. These UUIDs are to be used while setting up replication.
- Set up replication from the source universe by executing the following command on the source universe:

  ```sh
  kubectl exec -it -n <source_universe_namespace> -t <source_universe_master_leader> -c \
    <source_universe_container> -- bash -c "/home/yugabyte/bin/yb-admin -master_addresses \
    <target_universe_master_addresses> setup_universe_replication \
    <source_universe_UUID>_<replication_stream_name> <source_universe_master_addresses> \
    <comma_separated_table_ids>"
  ```

  Consider the following example:

  ```sh
  kubectl exec -it -n xcluster-source -t yb-master-2 -c yb-master -- bash -c \
    "/home/yugabyte/bin/yb-admin -master_addresses yb-master-2.yb-masters.xcluster-target.svc.cluster.local, \
    yb-master-1.yb-masters.xcluster-target.svc.cluster.local,yb-master-0.yb-masters.xcluster-target.svc.cluster.local \
    setup_universe_replication ac39666d-c183-45d3-945a-475452deac9f_xCluster_1 \
    yb-master-2.yb-masters.xcluster-source.svc.cluster.local,yb-master-1.yb-masters.xcluster-source.svc.cluster.local, \
    yb-master-0.yb-masters.xcluster-source.svc.cluster.local 00004000000030008000000000004001"
    ```

- Perform the following on the source universe and then observe replication on the target universe:

  ```sh
  kubectl exec -it -n <source_universe_namespace> -t <source_universe_master_leader> -c <source_universe_container> -- bash
    /home/yugabyte/bin/ysqlsh -h <source_universe_yqlserver>
    insert query
    select query
  ```

  Consider the following example:

  ```sh
  kubectl exec -it -n xcluster-source -t yb-master-2 -c yb-master -- bash
    /home/yugabyte/bin/ysqlsh -h yb-tserver-1.yb-tservers.xcluster-source
    INSERT INTO employees VALUES(1, 'name');
    SELECT * FROM employees;
  ```

- Perform the following on the target universe:

  ```sh
  kubectl exec -it -n <target_universe_namespace> -t <target_universe_master_leader> -c <target_universe_container> -- bash
    /home/yugabyte/bin/ysqlsh -h <target_universe_yqlserver>
    select query
  ```

  Consider the following example:

  ```sh
  kubectl exec -it -n xcluster-target -t yb-master-2 -c yb-master -- bash
    /home/yugabyte/bin/ysqlsh -h yb-tserver-1.yb-tservers.xcluster-target
    SELECT * FROM employees;
  ```

## Bootstrap a target universe

You can set up xCluster replication for the following purposes:

- Enabling replication on a table that has existing data.
- Catching up an existing stream where the target has fallen too far behind.

To ensure that the WALs are still available, you need to perform the following steps in the [cdc_wal_retention_time_secs](../../../../reference/configuration/yb-master/#cdc-wal-retention-time-secs) flag window. If the process is going to take more time than the value defined by this flag, you should increase the value.

Proceed as follows:

1. Create a checkpoint on the source universe for all the tables you want to replicate by executing the following command:

    ```sh
    ./bin/yb-admin -master_addresses <source_universe_master_addresses> \
    bootstrap_cdc_producer <comma_separated_source_universe_table_ids>
    ```

    Consider the following example:

    ```sh
    ./bin/yb-admin -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    bootstrap_cdc_producer 000033e1000030008000000000004000,000033e1000030008000000000004003,000033e1000030008000000000004006
    ```

    The following output is a list of bootstrap IDs, one per table ID:

    ```output
    table id: 000033e1000030008000000000004000, CDC bootstrap id: fb156717174941008e54fa958e613c10
    table id: 000033e1000030008000000000004003, CDC bootstrap id: a2a46f5cbf8446a3a5099b5ceeaac28b
    table id: 000033e1000030008000000000004006, CDC bootstrap id: c967967523eb4e03bcc201bb464e0679
    ```

1. Take the backup of the tables on the source universe and restore at the target universe by following instructions from [Backup and restore](../../../../manage/backup-restore/snapshot-ysql/).
1. Execute the following command to set up the replication stream using the bootstrap IDs generated in step 1. Ensure that the bootstrap IDs are in the same order as their corresponding table IDs.

    ```sh
    ./bin/yb-admin -master_addresses <target_universe_master_addresses> setup_universe_replication \
      <source_universe_uuid>_<replication_stream_name> <source_universe_master_addresses> \
      <comma_separated_source_universe_table_ids> <comma_separated_bootstrap_ids>
    ```

    Consider the following example:

    ```sh
    ./bin/yb-admin -master_addresses 127.0.0.11:7100,127.0.0.12:7100,127.0.0.13:7100 setup_universe_replication \
      00000000-1111-2222-3333-444444444444_xCluster1 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
      000033e1000030008000000000004000,000033e1000030008000000000004003,000033e1000030008000000000004006 \
      fb156717174941008e54fa958e613c10,a2a46f5cbf8446a3a5099b5ceeaac28b,c967967523eb4e03bcc201bb464e0679
    ```

### Modify the bootstrap

You can modify the bootstrap as follows:

- To wipe the test setup, use the `delete_universe_replication` command.
- After running the `bootstrap_cdc_producer` command on the source universe, you can verify that it work as expected by running the `list_cdc_streams` command to view the associated entries: the bootstrap IDs generated by the `bootstrap_cdc_producer` command should match the `stream_id` values you see after executing the `list_cdc_streams` command.

You can also perform the following modifications:

- To add a table to the source and target universes, use the `alter_universe_replication add_table` command. See [Handling DDL changes](#handling-ddl-changes).
- To remove an existing table from the source and target universes, use the `alter_universe_replication remove_table` command. See [Handling DDL changes](#handling-ddl-changes).
- To change master nodes on the source universe, execute the `alter_universe_replication set_master_addresses` command.
- You can verify changes via the `get_universe_config` command.

## Handling DDL changes

You can execute DDL operations after replication has been already been configured. Depending on the type of DDL operations, additional considerations are required.

### Adding new objects (Tables, Partitions, Indexes)

#### Adding tables (or partitions)

When new tables (or partitions) are created, to ensure that all changes from the time of object creation are replicated, writes should start on the new objects only after they are added to replication. If tables (or partitions) already have existing data before they are added to replication, then follow the bootstrap process described in [Bootstrap a target universe](#bootstrap-a-target-universe).

1. Create a table (with partitions) on both the source and target universes as follows:

    ```sql
    CREATE TABLE order_changes (
      order_id int,
      change_date date,
      type text,
      description text)
      PARTITION BY RANGE (change_date);

    CREATE TABLE order_changes_default PARTITION OF order_changes DEFAULT;

    --Create a new partition
    CREATE TABLE order_changes_2023_01 PARTITION OF order_changes
    FOR VALUES FROM ('2023-01-01') TO ('2023-03-30');
    ```

    Assume the parent table and default partition are included in the replication stream.

1. Get table IDs of the new partition from the source as follows:

    ```sql
    yb-admin -master_addresses <source_master_ips> \
    -certs_dir_name <cert_dir> \
    list_tables include_table_id|grep 'order_changes_2023_01'
    ```

    You should see output similar to the following:

    ```output
    yugabyte.order_changes_2021_01 000033e8000030008000000000004106
    ```

1. Add the new table (or partition) to replication.

   ```sql
   yb-admin -master_addresses <target_master_ips> \
   -certs_dir_name <cert_dir> \
   alter_universe_replication <replication_group_name> \
   add_table  000033e800003000800000000000410b
   ```

   You should see output similar to the following:

   ```output
    Replication altered successfully
   ```

#### Adding indexes

To add a new index to an empty table, follow the same steps as described in [Adding Tables (or Partitions)](#adding-tables-or-partitions).

However, to add a new index to a table that already has data, the following additional steps are required to ensure that the index has all the updates:

1. Create an [index](../../../../api/ysql/the-sql-language/statements/ddl_create_index/) - for example, `my_new index` on the source.
1. Wait for index backfill to finish. For more details, refer to YugabyteDB tips on [monitor backfill progress](https://yugabytedb.tips/?p=2215).
1. Determine the table ID for `my_new index`.

   ```sql
   yb-admin
   -master_addresses <source_master_ips> \
   -certs_dir_name <cert_dir> \
   list_tables include_table_id|grep 'my_new_index'
   ```

   You should see output similar to the following:

   ```output
   000033e8000030008000000000004028
   ```

1. Bootstrap the replication stream on the source using the `bootstrap_cdc_producer` API and provide the table ID of the new index as follows:

   ```sql
   yb-admin
   -master_addresses <source_master_ips> \
   -certs_dir_name <cert_dir> \
   bootstrap_cdc_producer 000033e8000030008000000000004028
   ```

   You should see output similar to the following:

   ```output
   table id: 000033e8000030008000000000004028, CDC bootstrap id: c8cba563e39c43feb66689514488591c
   ```

1. Wait for replication to be 0 on the main table using the replication lag metrics described in [Replication lag](#replication-lag).
1. Create an [index](../../../../api/ysql/the-sql-language/statements/ddl_create_index/) on the target.
1. Wait for index backfill to finish. For more details, refer to YugabyteDB tips on [monitor backfill progress](https://yugabytedb.tips/?p=2215).
1. Add the index to replication with the bootstrap ID from Step 4.

    ```sql
    yb-admin
    -master_addresses <target_master_ips> \
    -certs_dir_name <cert_dir> \
    alter_universe_replication 59e58153-eec6-4cb5-a858-bf685df52316_east-west \
    add_table  000033e8000030008000000000004028 c8cba563e39c43feb66689514488591c
    ```

   You should see output similar to the following:

    ```output
    Replication altered successfully
    ```

### Removing objects

Objects (tables, indexes, partitions) need to be removed from replication before they can be dropped as follows:

1. Get the table ID for the object to be removed from the source.

    ```sql
    yb-admin -master_addresses <source_master_ips> \
    -certs_dir_name <cert_dir> \
    list_tables include_table_id |grep '<partition_name>'
    ```

1. Remove the table from replication on the target.

    ```sql
    yb-admin -master_addresses <target_master_ips> \
    -certs_dir_name <cert_dir> \
    alter_universe_replication <replication_group_name> \
    remove_table  000033e800003000800000000000410b
    ```

### Alters

Alters involving adding/removing columns or modifying data types require replication to be paused before applying schema changes as follows:

1. Pause replication on both sides.

    ```sql
    yb-admin
    -master_addresses <target_master_ips>
    -certs_dir_name <cert_dir> \
    set_universe_replication_enabled <replication_group_name> 0
    ```

   You should see output similar to the following:

    ```output
    Replication disabled successfully
    ```

1. Perform the schema modification.
1. Resume replication as follows:

    ```sql
    yb-admin
    -master_addresses <target_master_ips>
    -certs_dir_name <cert_dir> \
    set_universe_replication_enabled <replication_group_name> 0
    ```

    ```output
    Replication enabled successfully
    ```
#### Adding a column with a non-volatile default value

When adding a new column with a (non-volatile) default expression, make sure to perform the schema modification on the target with the _computed_ default value. 

For example, say you have a replicated table `test_table`.

1. Pause replication on both sides. 
1. Execute add column command on the source:
   ```sql
   ALTER TABLE test_table ADD COLUMN test_column TIMESTAMP DEFAULT NOW()
   ```
1. Run the preceding `ALTER TABLE` command with the computed default value on the target as follows:
   
    - The computed default value can be retrieved from the `attmissingval` column in the `pg_attribute` catalog table.

      Example:
   
      ```sql
      SELECT attmissingval FROM pg_attribute WHERE attrelid='test'::regclass AND attname='test_column';
      ```
      ```output
                attmissingval         
       -------------------------------
        {"2024-01-09 12:29:11.88894"}
       (1 row)
      ```
   
    - Execute the `ADD COLUMN` command on the target with the computed default value.
      ```sql
         ALTER TABLE test ADD COLUMN test_column TIMESTAMP DEFAULT "2024-01-09 12:29:11.88894"
      ```

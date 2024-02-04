---
title: Deploy to two universes with transactional xCluster replication
headerTitle: Transactional xCluster deployment
linkTitle: Transactional xCluster
description: Enable deployment using transactional (active-standby) replication between universes
headContent: Transactional (active-standby) replication
menu:
  v2.18:
    parent: async-replication
    identifier: async-replication-transactional
    weight: 633
type: docs
---

A transactional xCluster deployment preserves and guarantees transactional atomicity and global ordering when propagating change data from one universe to another, as follows:

- Transactional atomicity guarantee. A transaction spanning tablets A and B will either be fully readable or not readable at all on the target universe. A and B can be tablets in the same table OR a table and an index OR different tables.

- Global ordering guarantee. Transactions are visible on the target side in the order they were committed on source.

Due to the asynchronous nature of xCluster replication, this deployment comes with non-zero recovery point objective (RPO) in the case of a source universe outage. The actual value depends on the replication lag, which in turn depends on the network characteristics between the regions.

The recovery time objective (RTO) is very low, as it only depends on the applications switching their connections from one universe to another. Applications should be designed in such a way that the switch happens as quickly as possible.

Transactional xCluster support further allows for the role of each universe to switch during planned and unplanned failover scenarios.

The xCluster role is a property with values ACTIVE or STANDBY that determines and identifies the active (source) and standby (target) universes:

- ACTIVE: The active universe serves both reads & writes. Reads/writes happen as of the latest time and according to the chosen isolation levels.
- STANDBY: The standby universe is meant for reads only. Reads happen as of xCluster safe time for the given database.

xCluster safe time is the transactionally consistent time across all tables in a given database at which Reads are served. In the following illustration, T1 is a transactionally consistent time across all tables.

![Transactional xCluster](/images/deploy/xcluster/xcluster-transactional.png)

The following assumes you have set up source and target universes. Refer to [Set up universes](../async-replication/#set-up-universes).

## Set up unidirectional transactional replication

Set up unidirectional transactional replication as follows:

1. Create a new database on the source and target universes with the same name.

1. Create the following tables and indexes on both source and target; the schema must match between the databases, including the number of tablets per table.

    ```sql
    create table ordering_test (id int) split into 8 tablets;

    create table customers (
      customer_id UUID,
      company_name text,
      customer_state text,
      primary key (customer_id)
    ) split into 8 tablets;

    create table orders (
      customer_id uuid,
      order_id int primary key,
      order_details jsonb,
      product_name text,
      product_code text,
      order_qty int,
      constraint fk_customer foreign key(customer_id) references customers(customer_id)
    ) split into 8 tablets;

    create index order_covering_index on orders (customer_id, order_id)
      include( order_qty )
      split into 8 tablets;

    create table if not exists account_balance (
      id uuid,
      name text,
      salary int,
      primary key (id)
    ) split into 8 tablets; 

    create index account_balance_secondary_index on account_balance(name)
    split into 8 tablets;

    create table if not exists account_sum ( sum int )
    split into 8 tablets;
    ```

1. Enable point in time restore (PITR) on the target database:

    ```sh
    ./bin/yb-admin \
        -master_addresses <target_master_addresses> \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

1. If the source universe already has data, then follow the bootstrap process described in [Bootstrap a target universe](../async-replication/#bootstrap-a-target-universe) before setting up replication with the transactional flag.

1. Set up xCluster replication from ACTIVE (source) to STANDBY (target) using yb-admin as follows:

    ```sh
    ./bin/yb-admin \
        -master_addresses <target_master_addresses> \
        -certs_dir_name <cert_dir> \
        setup_universe_replication \
        <source_universe_uuid>_<replication_name> \
        <source_universe_master_addresses> \
        <comma_separated_source_table_ids>  \
        <comma_separated_source_bootstrap_ids> transactional
    ```

1. Set the role of the target universe to STANDBY:

    ```sh
    ./bin/yb-admin \
        -master_addresses <target_master_addresses> \ 
        -certs_dir_name <dir_name> \
        change_xcluster_role STANDBY
    ```

1. On the target, verify `xcluster_safe_time` is progressing. This is the indicator of data flow from source to target.

    ```sh
    ./bin/yb-admin \
        -master_addresses <target_master_addresses>
        -certs_dir_name <dir_name> 
        get_xcluster_safe_time
    ```

    ```output.json
    $ ./tserver/bin/yb-admin -master_addresses 172.150.21.61:7100,172.150.44.121:7100,172.151.23.23:7100 get_xcluster_safe_time
    [
        {
            "namespace_id": "00004000000030008000000000000000",
            "namespace_name": "dr_db",
            "safe_time": "2023-06-07 16:31:17.601527",
            "safe_time_epoch": "1686155477601527"
        }
    ]
    ```

## Unplanned failover

Unplanned failover is the process of switching application traffic to the target (standby) universe in case the source (active) universe becomes unavailable. One of the common reasons for such a scenario is an outage of the source universe region.

Assuming universe A is the current source (active) universe and B is the current target (standby) universe, use the following steps to perform an unplanned failover on to B and resume applications from B:

1. When A is terminated, stop the application traffic to ensure no more updates are attempted.

1. Pause replication on B. This step is required to avoid unexpected updates arriving through replication, which can happen if A comes back up before the failover process is completed.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> 
        -certs_dir_name <cert_dir> \
        set_universe_replication_enabled <replication_name> 0
    ```

    Expect output similar to the following:

    ```output
    Replication disabled successfully
    ```

1. Get the latest consistent time on B. The `get_xcluster_safe_time` command provides an approximate value for the data loss expected to occur when the unplanned failover process finishes, and the consistent timestamp on the Standby universe.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> 
        -certs_dir_name <cert_dir> \
        get_xcluster_safe_time include_lag_and_skew
    ```

    Expect output similar to the following:

    ```output.json
    [
        {
            "namespace_id": "00004000000030008000000000000000",
            "namespace_name": "dr_db3",
            "safe_time": "2023-06-08 23:12:51.651318",
            "safe_time_epoch": "1686265971651318",
            "safe_time_lag_sec": "24.87",
            "safe_time_skew_sec": "1.16"
        }
    ]
    ```

1. Determine if the estimated data loss and the `safe_time` to which the system will be reset are acceptable.

1. Use PITR to restore the universe to a consistent state that cuts off any partially replicated transactions. Use the xCluster safe time obtained in the previous step as the "Restore" time in the following restore step.

    Find the Snapshot Schedule name for this database:

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \
        -certs_dir_name <cert_dir> \
        list_snapshot_schedules 
    ```

    Expect output similar to the following:

    ```output.json
    {
        "schedules": [
            {
                "id": "034bdaa2-56fa-44fb-a6da-40b1d9c22b49",
                "options": {
                    "filter": "ysql.dr_db2",
                    "interval": "1440 min",
                    "retention": "10080 min"
                },
                "snapshots": [
                    {
                        "id": "a83eca3e-e6a2-44f6-a9f2-f218d27f223c",
                        "snapshot_time": "2023-06-08 17:56:46.109379"
                    }
                ]
            }
        ]
    }
    ```

    Restore B to the minimum latest consistent time using PITR.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \
        -certs_dir_name <cert_dir> \
        restore_snapshot_schedule <schedule_id> "<safe_time>"
    ```

    Expect output similar to the following:

    ```output.json
    {
        "snapshot_id": "034bdaa2-56fa-44fb-a6da-40b1d9c22b49",
        "restoration_id": "e05e06d7-1766-412e-a364-8914691d84a3"
    }
    ```

1. Verify that restoration completed successfully by running the following command. Repeat this step until the restore state is RESTORED.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \
        -certs_dir_name <cert_dir> \
        list_snapshot_restorations
    ```

    Expect output similar to the following:

    ```output.json
    {
        "restorations": [
            {
                "id": "a3fdc1c0-3e07-4607-91a7-1527b7b8d9ea",
                "snapshot_id": "3ecbfc16-e2a5-43a3-bf0d-82e04e72be65",
                "state": "RESTORED"
            }
        ]
    }

1. Promote B to the active role:

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \
        -certs_dir_name <cert_dir> \
        change_xcluster_role ACTIVE
    ```

    ```output
    Changed role successfully
    ```

1. Delete the replication from A to B:

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \
        delete_universe_replication <A_universe_uuid>_<replication_name>
    ```

1. Resume the application traffic on B.

After completing the preceding steps, universe B becomes the new source (active) universe. There is no target (standby) until A comes back up and is restored to the correct state.

When A comes back up, to bring A into sync with B and set up replication in the opposite direction (from B to A), the database on A will need to be dropped and recreated from a backup of B (Bootstrap).

## Switchover (planned failover)

Planned switchover is the process of switching write access from the source (active) universe to the target (standby) universe without losing any data (zero RPO). Planned failover is typically performed during a maintenance window.

Assuming A is the source (active) universe and B is the target (standby), use the following steps to perform a planned failover.

### Stop and drain the universe

In the first stage, you drain pending updates and ensure that the replication lag is zero, thus guaranteeing zero RPO. You wait for pending updates to propagate to B by running the `wait_for_replication_drain` command on A.

Proceed as follows:

1. Stop the application traffic to ensure no more data changes are attempted.

1. Obtain the `stream_ids` for A:

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master_addresses> \
        -certs_dir_name <dir_name>  \
        list_cdc_streams
    ```

    Expect output similar to the following:

    ```output.json
    CDC Streams:
    streams {
      stream_id: "56bf794172da49c6804cbab59b978c7e"
      table_id: "000033e8000030008000000000004000"
      options {
        key: "record_type"
        value: "CHANGE"
      }
      options {
        key: "checkpoint_type"
        value: "IMPLICIT"
      }
      options {
        key: "record_format"
        value: "WAL"
      }
      options {
        key: "source_type"
        value: "XCLUSTER"
      }
      options {
        key: "state"
        value: "ACTIVE"
      }
    }
    ```

1. Run the following script to get the current time on A:

    ```python
    python3 checkTime.py

    import datetime 
    print(datetime.datetime.now().strftime("%s%f"))
    print(datetime.datetime.now())
    ```

    ```output
    1665548814177072
    2023-02-16 12:20:22.245984
    ```

1. Run the following command for each ACTIVE replication `stream_id` reported on A using the output of the two previous commands, in the form of a comma-separated list of the stream IDs and the timestamp:

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master_addresses> \
        -certs_dir_name <dir_name> \
        wait_for_replication_drain 56bf794172da49c6804cbab59b978c7e,..,..<<comma_separated_list_of_stream_ids>> 1665548814177072
    ```

    ```output
    All replications are caught-up.
    ```

    Note that this step may take some time.

    If you get output similar to the following, then it is not yet safe to switch universe B (current standby) to active:

    ```output
    Found undrained replications:
    - Under Stream b7306a8068484634801fd925a41f17f6:
      - Tablet: 04dd056e79a14f48aac3bdf196546fd0
      - Tablet: ebc07bd926b647a5ba8cea41421ec7ca
      - Tablet: 3687d4d51cda4f0394b4c36aa66f4ffd
      - Tablet: 318745aee32c463c81d4242924d679a2
    ```

1. Wait until xCluster safe time on B is greater than the current time obtained on A.

1. Use `change_xcluster_role` to promote B (current standby) to ACTIVE.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \ 
        -certs_dir_name <dir_name> \
        change_xcluster_role ACTIVE
    ```

1. Delete the replication from A to B:

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \
        delete_universe_replication <A_universe_uuid>_<replication_name>
    ```

### Set up replication

In the second stage, set up replication from the new active (B) universe as follows:

1. Run `bootstrap_cdc_producer` command on B for all tables involved in replication and capture the bootstrap IDs. This is a preemptive step to be used during the failover workflow while setting up replication from B to A. This prevents the WAL from being garbage collected and lets A catch up.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> 
        -certs_dir_name <cert_dir> \
        bootstrap_cdc_producer <comma_separated_B_table_ids>
    ```

1. Resume the application traffic on B.

1. Prepare certificates on all A nodes for replication setup:

    - Create a directory for the replication certificate:

        ```sh
        mkdir -p <home>/yugabyte-tls-producer/<B_universe_uuid>_<replicaten_name>
        ```

    - Copy the certificates:

        ```sh
        cp -rf <home>/yugabyte-tls-producer/<B_universe_uuid> <home>/yugabyte-tls-producer/<B_universe_uuid>_<replicaten_name>
        ```

    Repeat these steps on all A nodes.

1. Set up replication from B to A using the bootstrap IDs obtained previously.

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master_addresses> \
        -certs_dir_name <cert_dir> \
        setup_universe_replication \
        <B_universe_uuid>_<replication_name> \
        <B_master_addresses> \
        <comma_separated_B_table_ids>  \
        <comma_separated_B_bootstrap_ids> transactional
    ```

1. Use `change_xcluster_role` to demote A to STANDBY:

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master-addresses> \ 
        -certs_dir_name <dir_name> \
        change_xcluster_role STANDBY
    ```

After completing the preceding steps, universe B becomes the new source (active) universe, and universe A becomes the new target (standby) universe. No updates are lost during the process, so the RPO is zero. There is expected to be no data loss in this workflow.

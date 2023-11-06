---
title: Unplanned failover with transactional xCluster replication
headerTitle: Planned switchover
linkTitle: Switchover
description: Switchover using transactional (active-standby) replication between universes
headContent: Switch application traffic to the standby universe
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-switchover
    weight: 40
type: docs
---

Planned switchover is the process of switching write access from the Primary universe to the Standby universe without losing any data (zero RPO). Planned switchover is typically performed during a maintenance window.

## Manual switchover

Assuming universe A is the Primary and universe B is the Standby, use the following steps to perform a planned switchover.

### Stop and drain the universe

You start by draining pending updates and ensuring that the replication lag is zero, thus guaranteeing zero RPO. You wait for pending updates to propagate to B by running the `wait_for_replication_drain` command on A.

Proceed as follows:

1. Stop the application traffic to ensure no more data changes are attempted.

1. Obtain the `stream_ids` for Primary (A):

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

1. Run the following script to get the current time on Primary (A):

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

1. Run the following command for each ACTIVE replication `stream_id` reported on Primary (A) using the output of the two previous commands, in the form of a comma-separated list of the stream IDs and the timestamp:

    ```sh
    ./bin/yb-admin \
        -master_addresses <A_master_addresses> \
        -certs_dir_name <dir_name> \
        wait_for_replication_drain 56bf794172da49c6804cbab59b978c7e,..,..<comma_separated_list_of_stream_ids> 1665548814177072
    ```

    ```output
    All replications are caught-up.
    ```

    Note that this step may take some time.

    If you get output similar to the following, then it is not yet safe to switch Standby (B) to active:

    ```output
    Found undrained replications:
    - Under Stream b7306a8068484634801fd925a41f17f6:
      - Tablet: 04dd056e79a14f48aac3bdf196546fd0
      - Tablet: ebc07bd926b647a5ba8cea41421ec7ca
      - Tablet: 3687d4d51cda4f0394b4c36aa66f4ffd
      - Tablet: 318745aee32c463c81d4242924d679a2
    ```

1. Wait until [xCluster safe time](../async-transactional-setup/#verify-replication) on Standby (B) is greater than the current time obtained on Primary (A).

1. Use `change_xcluster_role` to promote Standby (B) to ACTIVE as follows.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \ 
        -certs_dir_name <dir_name> \
        change_xcluster_role ACTIVE
    ```

1. Delete the replication from A to B as follows:

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> \
        delete_universe_replication <A_universe_uuid>_<replication_name>
    ```

### Set up replication

In the second stage, set up replication from the new Primary (B) universe as follows:

1. Run `bootstrap_cdc_producer` command on the new Primary (B) for all tables involved in replication and capture the bootstrap IDs. This is a preemptive step to be used during the failover workflow while setting up replication from B to A. This prevents the WAL from being garbage collected and lets A catch up.

    ```sh
    ./bin/yb-admin \
        -master_addresses <B_master_addresses> 
        -certs_dir_name <cert_dir> \
        bootstrap_cdc_producer <comma_separated_B_table_ids>
    ```

1. Resume the application traffic on the new Primary (B).

1. Prepare certificates on all nodes of A for replication setup:

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

After completing the preceding steps, the Standby (B) becomes the new Primary universe, and the old Primary (A) becomes the new Standby universe. No updates are lost during the process, so the RPO is zero. There is expected to be no data loss in this workflow.

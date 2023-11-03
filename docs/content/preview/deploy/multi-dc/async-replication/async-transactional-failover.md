---
title: Unplanned failover with transactional xCluster replication
headerTitle: Unplanned failover
linkTitle: Failover
description: Unplanned failover using transactional (active-standby) replication between universes
headContent: Failover of application traffic to the standby universe
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-failover
    weight: 30
type: docs
---

Unplanned failover is the process of switching application traffic to the Standby universe in case the Primary universe becomes unavailable. One of the common reasons for such a scenario is an outage of the Primary universe region.

## Performing failover

Assuming universe A is the Primary and universe B is the Standby, use the following procedure to perform an unplanned failover and resume applications on B.

### Pause replication and get the safe time

If the Primary is Terminated for some reason, do the following:

1. Stop the application traffic to ensure no more updates are attempted.

1. Pause replication on the Standby (B). This step is required to avoid unexpected updates arriving through replication, which can happen if the Primary comes back up before the failover process is completed.

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
        -certs_dir_name <cert_dir> \
        set_universe_replication_enabled <replication_name> 0
    ```

    Expect output similar to the following:

    ```output
    Replication disabled successfully
    ```

1. Get the latest consistent time on Standby (B). The `get_xcluster_safe_time` command provides an approximate value for the data loss expected to occur when the unplanned failover process finishes, and the consistent timestamp on the Standby universe.

    ```sh
    yb-admin \
    -master_addresses <standby_master_addresses> \
    -certs_dir_name <cert_dir> \
    get_xcluster_safe_time include_lag_and_skew
    ```

    Example Output:

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

    `safe_time_lag_sec` is the time elapsed in microseconds between the physical time and safe time. Safe time is when data has been replicated to all the tablets on the target cluster.

    `safe_time_skew_sec` is the time elapsed in microseconds for replication between the first and the last tablet replica on the target cluster.

    Determine if the estimated data loss and the safe time to which the system will be reset are acceptable.

### Restore the cluster to a consistent state

Use PITR to restore the cluster to a consistent state that cuts off any partially replicated transactions. Use the xCluster `safe_time` obtained in the previous step as the restore time.

If there are multiple databases in the same cluster, do PITR on all the databases sequentially (one after the other).

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#local" class="nav-link active" id="local-tab" data-toggle="tab"
      role="tab" aria-controls="local" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="#anywhere" class="nav-link" id="anywhere-tab" data-toggle="tab"
      role="tab" aria-controls="anywhere" aria-selected="false">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="local" class="tab-pane fade show active" role="tabpanel" aria-labelledby="local-tab">

To do a PITR on a database:

1. On Standby (B), find the Snapshot Schedule name for this database:

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
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

1. Restore Standby (B) to the minimum latest consistent time using PITR.

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
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
        -master_addresses <standby_master_addresses> \
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

1. Promote Standby (B) to the active role:

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
        -certs_dir_name <cert_dir> \
        change_xcluster_role ACTIVE
    ```

    ```output
    Changed role successfully
    ```

1. Delete the replication from the former Primary (A):

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
        delete_universe_replication <primary_universe_uuid>_<replication_name>
    ```

1. Resume the application traffic on the new Primary (B).

  </div>

  <div id="anywhere" class="tab-pane fade" role="tabpanel" aria-labelledby="cloud-tab">

To do a PITR on a database:

1. In YugabyteDB Anywhere, navigate to Standby (B) and choose **Backups > Point-in-time Recovery**.

1. For the database or keyspace, click **...** and choose **Recover to a Point in Time**.

1. Select the Date and Time option and enter the safe time you obtained.

    By default, YBA shows the time in local time, whereas yb-admin returns the time in UTC. Therefore, you need to convert the xCluster Safe time to local time when entering the date and time.

    Alternatively, you can configure YBA to use UTC time (always) by changing the Preferred Timezone to UTC in the **User > Profile** settings.

    Note that YBA only supports minute-level granularity.

1. Click **Recover**.

1. Promote Standby (B) to ACTIVE as follows:

    ```sh
    yb-admin \
    -master_addresses <standby_master_addresses> \
    -certs_dir_name <cert_dir> \
    change_xcluster_role ACTIVE
    ```

    ```output
    Changed role successfully
    ```

    The former Standby (B) is now the new Primary.

1. In YugabyteDB Anywhere, navigate to the new Primary (B), choose **Replication**, and select the replication.

1. Click **Actions > Delete Replication**.

1. If the former Primary (A) is completely down and not reachable, select the **Ignore errors and force delete** option.

1. Click **Delete Replication**.

    If the old Primary (A) is unreachable, it takes longer to show the replication page on the UI. In this case, an error similar to the following is displayed. You can either pause or delete the replication configuration.

    ![Primary unreachable](/images/yp/create-deployments/xcluster/deploy-xcluster-tran-unreachable.png)

1. Resume the application traffic on the new Primary (B).

  </div>

</div>

After completing the preceding steps, the former Standby (B) is the new Primary (active) universe. There is no target (standby) until the former Primary (A) comes back up and is restored to the correct state.

## Set up reverse replication

If the former Primary universe (A) doesn't come back and you end up creating a new cluster in place of A, follow the [steps for a fresh replication setup](../async-transactional-setup/).

If universe A is brought back, to bring A into sync with B and set up replication in the opposite direction (B->A), the database on A needs to be dropped and recreated from a backup of B (Bootstrap).

Do the following:

1. In YugabyteDB Anywhere, navigate to universe A and choose **Backups > Point-in-time Recovery**.

1. For the database or keyspace, click **...** and choose **Disable Point-in-time Recovery**.

1. Drop the database(s) on A.

    If the original cluster A went down and could not clean up any of its replication streams, then dropping the database may fail with the following error:

    ```output
    ERROR: Table: 00004000000030008000000000004037 is under replication
    ```

    If this happens, do the following using yb-admin to clean up replication streams on A:

    - List the CDC streams on Cluster A to obtain the list of stream IDs as follows:

        ```sh
        yb-admin \
        -master_addresses <A_master_ips> \
        -certs_dir_name <dir_name> \
        list_cdc_streams
        ```

    - For each stream ID, delete the corresponding CDC stream:

        ```sh
        yb-admin \
        -master_addresses <A_master_ips> \
        -certs_dir_name <dir_name> \
        delete_cdc_stream <streamID>
        ```

1. Recreate the database(s) on A.

    DO NOT create any tables. Replication setup (Bootstrapping) will create tables/objects on A from B.

1. In YugabyteDB Anywhere, enable PITR on all replicating databases on both Primary and Standby universes.

1. In YugabyteDB Anywhere, set up xCluster Replication from the ACTIVE to STANDBY universe (B to A).

1. On the Standby (A), verify `xcluster_safe_time` is progressing. This is the indicator of data flow from Primary to Standby.

    ```sh
    yb-admin \
    -master_addresses <standby_master_ips> \
    -certs_dir_name <dir_name> \
    get_xcluster_safe_time
    ```

    For example:

    ```sh
    ./yb-admin -master_addresses 172.150.21.61:7100,172.150.44.121:7100,172.151.23.23:7100 get_xcluster_safe_time
    ```

    ```output.json
    [
        {
            "namespace_id": "00004000000030008000000000000000",
            "namespace_name": "dr_db",
            "safe_time": "2023-06-07 16:31:17.601527",
            "safe_time_epoch": "1686155477601527"
        }
    ]
    ```

    Default time reported in xCluster safe time is year 2112. Need to wait until the xCluster safe time on Standby advances beyond setup replication clock time.

Replication is now complete.

If your eventual desired configuration is for A to be the primary cluster and B the standby, follow the steps for [Planned Switchover](../async-transactional-switchover/).

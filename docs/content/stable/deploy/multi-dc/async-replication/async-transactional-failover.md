---
title: Unplanned failover with transactional xCluster replication
headerTitle: Unplanned failover
linkTitle: Failover
description: Unplanned failover using transactional xCluster replication between universes
headContent: Failover of application traffic to the standby universe with potential data loss
menu:
  stable:
    parent: async-replication-transactional
    identifier: async-transactional-failover
    weight: 30
tags:
  other: ysql
type: docs
---

Unplanned failover is the process of promoting the Standby universe and switching application traffic to it in case the Primary universe becomes unavailable. One of the common reasons for such a scenario is an outage of the Primary universe region.

{{< warning title="Warning" >}}
Failover has a non-zero RPO, meaning it can result in the loss of committed data.

Failover should only be performed when you are certain the Primary universe will not recover soon, and the data loss is acceptable.
{{< /warning >}}

## Performing failover

Assuming universe A is the Primary and universe B is the Standby, use the following procedure to perform an unplanned failover and resume applications on B.

### Stop application traffic to A

Stop the application traffic to ensure no more updates are attempted.

### Pause the replication

Pause replication on the Standby (B). This step is required to avoid unexpected updates arriving through replication, which can happen if the Primary (A) comes back up before failover completes, or if A was partially down.

```sh
./bin/yb-admin \
    --master_addresses <B-master-addresses> \
    set_universe_replication_enabled <replication_name> 0
```

Expect output similar to the following:

```output
Replication disabled successfully
```

### Get the safe time

Get the latest consistent time on Standby (B). The `get_xcluster_safe_time` command provides an approximate value for the data loss expected to occur when the unplanned failover process finishes, and the consistent timestamp on the Standby universe.

```sh
./bin/yb-admin \
    --master_addresses <B-master-addresses> \
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

`safe_time` represents the latest timestamp at which all tables in the namespace have been transactionally replicated to the Standby universe. Failover will restore the namespace to this time.

`safe_time_lag_sec` represents the time in seconds between the current time and `safe_time`. This indicates how long the Primary universe has been unavailable.

`safe_time_skew_sec` represents the difference in seconds between the most and least caught up tablet on the Standby universe. Unless the replication lag was high before the failure, it serves as a measure of estimated data that will be lost on failover.

Determine whether the estimated data loss is acceptable. If you know the exact time the Primary universe went down, you can compare it to the `safe_time`. Otherwise, use `safe_time_skew_sec` as a rough measure to estimate the data loss.

For more information on replication metrics, refer to [xCluster metrics](../../../../launch-and-manage/monitor-and-alert/metrics/replication/).

### Restore the database(s) on B to a consistent state

Use PITR to restore the universe to a consistent state that cuts off any partially replicated transactions. Use the xCluster `safe_time` obtained in the previous step as the restore time.

If there are multiple databases, perform PITR on each database sequentially, one after the other.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
<li>
    <a href="#yugabyted-pitr" class="nav-link active" id="yugabyted-pitr-tab" data-bs-toggle="tab"
    role="tab" aria-controls="yugabyted-pitr" aria-selected="true">
    <img src="/icons/database.svg" alt="Server Icon">
    yugabyted
    </a>
</li>
<li>
    <a href="#local-pitr" class="nav-link" id="local-pitr-tab" data-bs-toggle="tab"
    role="tab" aria-controls="local-pitr" aria-selected="false">
    <i class="icon-shell"></i>
    Manual
    </a>
</li>
</ul>
<div class="tab-content">
<div id="yugabyted-pitr" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-pitr-tab">

Restore the database to the `safe_time`:

```sh
./bin/yugabyted restore \
    --database <database> \
    --recover_to_point_in_time '<safe_time>'
```

</div>

<div id="local-pitr" class="tab-pane fade " role="tabpanel" aria-labelledby="local-pitr-tab">

1. Find the Snapshot Schedule name for the database:

    ```sh
    ./bin/yb-admin \
        --master_addresses <B-master-addresses> \
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

1. Restore to the `safe_time` of the database:

    ```sh
    ./bin/yb-admin \
        --master_addresses <B-master-addresses> \
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
        --master_addresses <B-master-addresses> \
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
    ```

</div>
</div>

### Delete the old replication group

```sh
./bin/yb-admin \
    --master_addresses <B-master-addresses> \
    delete_universe_replication <replication_group_id>
```

### Fix up sequences and serial columns

{{< note >}}
Skip this step if you are using xCluster replication automatic mode.
{{< /note >}}

xCluster only replicates sequence data in automatic mode.  If you are not using automatic mode, you need to manually adjust the sequence next values on universe B after failover to ensure consistency with the tables using them.

For example, if you have a SERIAL column in a table and the highest value in that column after failover is 500, you need to set the sequence associated with that column to a value higher than 500, such as 501. This ensures that new writes on universe B do not conflict with existing data.

Use the [nextval](../../../../api/ysql/exprs/sequence_functions/func_nextval/) function to set the sequence next values appropriately.

### Fix CDC

If you are using CDC to move data out of YugabyteDB, note that failover may incur data loss for your CDC replication; data lost on the CDC target may be different from data lost on the xCluster target.

You can fix CDC in either of the following ways:

- Start CDC on B (that is, create publications and slots on B). This resumes CDC from the failover point (subject to possible data loss).

- Clear your CDC target of all data, and start CDC on B from a fresh copy, making another full copy.

    Then point your CDC target to pull from B (the newly promoted database).

### Switch applications to B

Update the application connection strings to point to the new Primary universe (B).

After completing the preceding steps, the former Standby (B) is the new Primary (active) universe. There is no Standby until the former Primary (A) comes back up and is restored to the correct state.

## Set up reverse replication after A recovers

If the former Primary universe (A) doesn't come back and you end up creating a new cluster in place of A, follow the steps in [Set up transactional xCluster](../async-transactional-setup-automatic/).

If universe A is brought back, to bring A into sync with B and set up replication in the opposite direction (B->A), the databases on A need to be dropped and recreated from a backup of B (bootstrap). Before dropping the databases on A, you can analyze them to determine the exact data that was lost by the failover.

When you are ready to drop the databases on A, proceed to the next steps.

### Disable PITR

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
<li>
    <a href="#yugabyted-drop-pitr" class="nav-link active" id="yugabyted-drop-pitr-tab" data-bs-toggle="tab"
    role="tab" aria-controls="yugabyted-drop-pitr" aria-selected="true">
    <img src="/icons/database.svg" alt="Server Icon">
    yugabyted
    </a>
</li>
<li>
    <a href="#local-drop-pitr" class="nav-link" id="local-drop-pitr-tab" data-bs-toggle="tab"
    role="tab" aria-controls="local-drop-pitr" aria-selected="false">
    <i class="icon-shell"></i>
    Manual
    </a>
</li>
</ul>
<div class="tab-content">
<div id="yugabyted-drop-pitr" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-drop-pitr-tab">

Disable point-in-time recovery for the database(s) on A:

```sh
./bin/yugabyted configure point_in_time_recovery \
    --disable \
    --database <database_name>
```

</div>

<div id="local-drop-pitr" class="tab-pane fade " role="tabpanel" aria-labelledby="local-drop-pitr-tab">

- List the snapshot schedules to obtain the schedule ID:

    ```sh
    ./bin/yb-admin --master_addresses <A-master-addresses> \
        list_snapshot_schedules
    ```

- Delete the snapshot schedule:

    ```sh
    ./bin/yb-admin \
        --master_addresses <A-master-addresses> \
        delete_snapshot_schedule <schedule_id>
    ```

</div>
</div>

### Drop xCluster streams

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
<li>
    <a href="#dbscoped-drop" class="nav-link active" id="dbscoped-drop-tab" data-bs-toggle="tab"
    role="tab" aria-controls="dbscoped-drop" aria-selected="true">
    Automatic/Semi-automatic
    </a>
</li>
<li>
    <a href="#manual-drop" class="nav-link" id="manual-drop-tab" data-bs-toggle="tab"
    role="tab" aria-controls="manual-drop" aria-selected="false">
    Fully Manual
    </a>
</li>
</ul>
<div class="tab-content">
<div id="dbscoped-drop" class="tab-pane fade show active" role="tabpanel" aria-labelledby="dbscoped-drop-tab">

Drop the replication group on A using the following command:

```sh
./bin/yb-admin \
    --master_addresses <A-master-addresses> \
    drop_xcluster_replication <replication_group_id>
```

You should see output similar to the following:

```output
Outbound xCluster Replication group rg1 deleted successfully
```

</div>

<div id="manual-drop" class="tab-pane fade " role="tabpanel" aria-labelledby="local-drop-tab">

- List the CDC streams on A to obtain the list of stream IDs as follows:

    ```sh
    ./bin/yb-admin \
    --master_addresses <A_master_ips> \
    list_cdc_streams
    ```

- For each stream ID, delete the corresponding CDC stream:

    ```sh
    ./bin/yb-admin \
    --master_addresses <A_master_ips> \
    delete_cdc_stream <streamID>
    ```

</div>
</div>

### Drop the database(s)

Drop the database(s) on A.

### Perform setup from B to A

Set up xCluster Replication from the Primary to Standby universe (B to A) by following the steps in [Set up transactional xCluster](../async-transactional-setup-automatic/).

If your eventual desired configuration is for A to be the Primary universe and B the Standby, follow the steps for [Planned switchover](../async-transactional-switchover/).

---
title: Deploy transactional xCluster
headerTitle: Deploy transactional xCluster
linkTitle: Deploy
description: Setting up transactional (active-active single-master) replication between two YB universes
headContent: Set up transactional xCluster replication
menu:
  preview:
    parent: async-replication-transactional
    identifier: async-transactional-setup-1-automatic
    weight: 10
tags:
  other: ysql
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../async-transactional-setup-automatic/" class="nav-link active">
      Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-semi-automatic/" class="nav-link">
      Semi-Automatic
    </a>
  </li>
  <li >
    <a href="../async-transactional-setup-manual/" class="nav-link">
      Manual
    </a>
  </li>
</ul>

{{< note title="Note" >}}
To use automatic transactional xCluster replication, both the Primary and Standby universes must be running v2.25.1 or later.
{{< /note >}}


Automatic transactional xCluster replication {{<tags/feature/tp>}} handles all aspects of replication for both and schema changes.

In particular, DDL changes made to the Primary universe is automatically replicated to the Standby universe.

In this mode, xCluster replication operates at the YSQL database granularity. This means you only run xCluster management operations when adding and removing databases from replication, and not when tables in the databases are created or dropped.


## Set up replication

Since this feature is in [technical preview](/preview/releases/versioning/#tech-preview-tp), you must enable it by adding the `xcluster_enable_ddl_replication` flag to the [allowed_preview_flags_csv](../../../reference/configuration/yb-master/#allowed-preview-flags-csv) list and setting it to true on yb-master.


<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted" class="nav-link active" id="yugabyted-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>
  <li>
    <a href="#local" class="nav-link" id="local-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local" aria-selected="false">
      <i class="icon-shell"></i>
      Local
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-tab">

<!-- YugabyteD Setup -->

The following assumes you have set up Primary and Standby universes. Refer to [Set up yugabyted universes](../../../../reference/configuration/yugabyted/#start). The yugabyted node must be started with `--backup_daemon=true` to initialize the backup/restore agent.


1. Create a checkpoint on the Primary universe for all the databases that you want to be part of the replication.

    ```sh
    ./bin/yugabyted xcluster create_checkpoint \
        --replication_id <replication_id> \
        --databases <comma_separated_database_names>
        --automatic_mode
    ```

    The command informs you if any data needs to be copied to the Standby, or only the schema (empty tables and indexes) needs to be created. For example:

    ```output
    +-------------------------------------------------------------------------+
    |                                yugabyted                                |
    +-------------------------------------------------------------------------+
    | Status               : xCluster create checkpoint success.              |
    | Bootstrapping        : Bootstrap is required for database `yugabyte`.   |
    +-------------------------------------------------------------------------+

    For each database which requires bootstrap run the following commands to perform a backup and restore.
    # Run on source:
    ./yugabyted backup --cloud_storage_uri <AWS/GCP/local cloud storage uri>  --database <database_name> --base_dir <base_dir of source node>
    # Run on target:
    ./yugabyted restore --cloud_storage_uri <AWS/GCP/local cloud storage uri>  --database <database_name> --base_dir <base_dir of target node>
    ```

1. If needed, perform a full copy of the database(s) on the Primary to the Standby using distributed [backup](../../../../reference/configuration/yugabyted/#backup) and [restore](../../../../reference/configuration/yugabyted/#restore). If your source database is not empty, it must be bootstrapped, even if the output suggests otherwise. This applies even if it contains only empty tables, unused types, or enums (#24030).

1. Enable [point in time restore (PITR)](../../../../manage/backup-restore/point-in-time-recovery/) on the database(s) on both the Primary and Standby universes:

    ```sh
    ./bin/yugabyted configure point_in_time_recovery \
        --enable \
        --retention <retention_period> \
        --database <database_name>
    ```

    The `retention_period` must be greater than the amount of time you expect the Primary universe to be down before it self recovers or before you perform a failover to the Standby universe.

1. Set up the xCluster replication.

    ```sh
    ./bin/yugabyted xcluster set_up \
        --target_address <ip_of_any_target_cluster_node> \
        --replication_id <replication_id> \
        --bootstrap_done
    ```

    You should see output similar to the following:

    ```output
    +-----------------------------------------------+
    |                   yugabyted                   |
    +-----------------------------------------------+
    | Status        : xCluster set-up successful.   |
    +-----------------------------------------------+

    ```
  </div>

  <div id="local" class="tab-pane fade " role="tabpanel" aria-labelledby="local-tab">

<!-- Local Setup -->

The following assumes you have set up Primary and Standby universes. Refer to [Set up universes](../async-deployment/#set-up-universes).

1. Create a checkpoint using the `create_xcluster_checkpoint` command, providing a name for the replication group, and the names of the databases to replicate as a comma-separated list.

    ```sh
    ./bin/yb-admin \
        -master_addresses <primary_master_addresses> \
        create_xcluster_checkpoint <replication_group_id> <comma_separated_namespace_names>
    ```

    The command informs you if any data needs to be copied to the Standby, or only the schema (empty tables and indexes) needs to be created. For example:

    ```output
    Waiting for checkpointing of database(s) to complete

    Checkpointing of yugabyte completed. Bootstrap is not required for setting up xCluster replication
    Successfully checkpointed databases for xCluster replication group repl_group1

    Create equivalent YSQL objects (schemas, tables, indexes, ...) for databases [yugabyte] on the standby universe
    Once the above step(s) complete run 'setup_xcluster_replication'
    ```

    You can also manually check the status as follows:

    ```sh
    ./bin/yb-admin \
    -master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    is_xcluster_bootstrap_required repl_group1 yugabyte
    ```

    You should see output similar to the following:

    ```output
    Waiting for checkpointing of database(s) to complete

    Checkpointing of yugabyte completed. Bootstrap is not required for setting up xCluster replication
    ```

1. If needed, perform a full copy of the database on the Primary to the Standby using distributed backup and restore. See [Distributed snapshots for YSQL](../../../../manage/backup-restore/snapshot-ysql/). Otherwise, create the necessary schema objects (tables and indexes) on the Standby.

1. Enable [point in time restore (PITR)](../../../../admin/yb-admin/#create-snapshot-schedule) on the database(s) on both the Primary and Standby universes:

    ```sh
    ./bin/yb-admin \
        -master_addresses <standby_master_addresses> \
        create_snapshot_schedule \
        <snapshot-interval> \
        <retention-time> \
        <ysql.database_name>
    ```

    The `retention-time` must be greater than the amount of time you expect the Primary universe to be down before it self recovers or before you perform a failover to the Standby universe.

1. Set up the xCluster replication group.

    ```sh
    ./bin/yb-admin \
    -master_addresses <primary_master_addresses> \
    setup_xcluster_replication <replication_group_id> <standby_master_addresses> \
    automatic_ddl_mode
    ```

    You should see output similar to the following:

    ```output
    xCluster Replication group repl_group1 setup successfully
    ```


  </div>

</div>


## Making DDL changes

DDL operations must only be performed on the Primary universe. All schema changes are automatically replicated to the Standby universe.

## Limitations

For more information on the YugabyteDB xCluster implementation and its limitations, refer to [xCluster implementation limitations](../../../../architecture/docdb-replication/async-replication/#limitations).

<!--
+++
private = true
block_indexing: true
+++
-->

{{< tip >}}
Before setting up xCluster replication, ensure you have reviewed the [Prerequisites](../#prerequisites) and [Best practices](../#best-practices).
{{< /tip >}}

{{< note >}}
DDLs must be paused on the Primary universe during the entire set up process. {{<issue 26053>}}
{{< /note >}}

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-setup" class="nav-link active" id="yugabyted-setup-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-setup" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      yugabyted
    </a>
  </li>
  <li>
    <a href="#local-setup" class="nav-link" id="local-setup-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-setup" aria-selected="false">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted-setup" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-setup-tab">

<!-- YugabyteD Setup -->

The following assumes you have set up Primary and Standby universes. Refer to [Set up universes](../async-deployment/#set-up-universes).

1. Create a checkpoint on the Primary universe for all the databases that you want to be part of the replication.

    ```sh
    ./bin/yugabyted xcluster create_checkpoint \
        --replication_id <replication_id> \
        --databases <comma_separated_database_names>
    ```

    The command informs you if any data needs to be copied to the Standby, or only the schema (empty tables and indexes) needs to be created.

    For example:

    ```output
    +-------------------------------------------------------------------------+
    |                                yugabyted                                |
    +-------------------------------------------------------------------------+
    | Status               : xCluster create checkpoint success.              |
    | Bootstrapping        : Bootstrap is required for database `yugabyte`.   |
    +-------------------------------------------------------------------------+
    For each database which requires bootstrap run the following commands to perform a backup and restore.
    Run on source:
    ./yugabyted backup --cloud_storage_uri <AWS/GCP/local cloud storage uri>  --database <database_name> --base_dir <base_dir of source node>
    Run on target:
    ./yugabyted restore --cloud_storage_uri <AWS/GCP/local cloud storage uri>  --database <database_name> --base_dir <base_dir of target node>
    ```

1. If needed, perform a full copy of the database(s) on the Primary to the Standby using distributed [backup](../../../../reference/configuration/yugabyted/#backup) and [restore](../../../../reference/configuration/yugabyted/#restore).

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

  <div id="local-setup" class="tab-pane fade " role="tabpanel" aria-labelledby="local-setup-tab">

<!-- Manual Setup -->
The following assumes you have set up Primary and Standby universes. Refer to [Set up universes](../async-deployment/#set-up-universes).

1. Create a checkpoint using the `create_xcluster_checkpoint` command, providing a name for the replication group, and the names of the databases to replicate as a comma-separated list.

    ```sh
    ./bin/yb-admin \
        --master_addresses <primary_master_addresses> \
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
    --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    is_xcluster_bootstrap_required repl_group1 yugabyte
    ```

    You should see output similar to the following:

    ```output
    Waiting for checkpointing of database(s) to complete
    Checkpointing of yugabyte completed. Bootstrap is not required for setting up xCluster replication
    ```

1. If needed, perform a full copy of the database on the Primary to the Standby using distributed backup and restore. See [Distributed snapshots for YSQL](../../../../manage/backup-restore/snapshot-ysql/#move-a-snapshot-to-external-storage). Otherwise, create the necessary schema objects (tables and indexes) on the Standby.

1. Enable [point in time restore (PITR)](../../../../manage/backup-restore/point-in-time-recovery/) on the database(s) on both the Primary and Standby universes:

    ```sh
    ./bin/yb-admin \
        --master_addresses <standby_master_addresses> \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

1. Set up the xCluster replication group.

    ```sh
    ./bin/yb-admin \
    --master_addresses <primary_master_addresses> \
    setup_xcluster_replication <replication_group_id> <standby_master_addresses>
    ```

    You should see output similar to the following:

    ```output
    xCluster Replication group repl_group1 setup successfully
    ```

  </div>
</div>

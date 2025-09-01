<!--
+++
private = true
block_indexing = true
+++
-->

The database should have at least one table in order to be added to replication. If it is a colocated database then there should be at least one colocated table in the database in order for it to be added to replication.

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-add-db" class="nav-link active" id="yugabyted-add-db-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-add-db" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      yugabyted
    </a>
  </li>
  <li>
    <a href="#local-add-db" class="nav-link" id="local-add-db-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-add-db" aria-selected="false">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted-add-db" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-add-db-tab">

<!-- YugabyteD Add DB -->
1. Create a checkpoint on the Primary universe for all the databases that you want to add to an existing replication group.

    ```sh
    ./bin/yugabyted xcluster add_to_checkpoint \
        --replication_id <replication_id> \
        --databases <comma_separated_database_names>
    ```

    You should see output similar to the following:

    ```output
    Waiting for checkpointing of database to complete
    Successfully checkpointed database db2 for xCluster replication group repl_group1
    Bootstrap is not required for adding database to xCluster replication
    Create equivalent YSQL objects (schemas, tables, indexes, ...) for the database in the standby universe
    ```

1. If bootstrapping is required, perform a full copy of the database(s) on the Primary to the Standby using distributed [backup](../../../../reference/configuration/yugabyted/#backup) and [restore](../../../../reference/configuration/yugabyted/#restore). If your source database is not empty or you are using automatic mode, it must be bootstrapped.

1. Enable [point in time restore (PITR)](../../../../manage/backup-restore/point-in-time-recovery/) on the database(s) on both the Primary and Standby universes:

    ```sh
    ./bin/yugabyted configure point_in_time_recovery \
        --enable \
        --retention <retention_period> \
        --database <database_name>
    ```

    The `retention_period` must be greater than the amount of time you expect the Primary universe to be down before it self recovers or before you perform a failover to the Standby universe.

1. Add the databases to the xCluster replication.

    ```sh
    ./bin/yugabyted xcluster add_to_replication \
        --databases <comma_separated_database_names> \
        --replication_id <replication_id> \
        --target_address <IP-of-any-target-node> \
        --bootstrap_done
    ```

  </div>

  <div id="local-add-db" class="tab-pane fade " role="tabpanel" aria-labelledby="local-add-db-tab">

<!-- Manual Add DB -->

1. Create a checkpoint.

    ```sh
    ./bin/yb-admin \
    --master_addresses <primary_master_addresses> \
    add_namespace_to_xcluster_checkpoint <replication_group_id> <namespace_name>
    ```

    You should see output similar to the following:

    ```output
    Waiting for checkpointing of database to complete
    Successfully checkpointed database db2 for xCluster replication group repl_group1
    Bootstrap is not required for adding database to xCluster replication
    Create equivalent YSQL objects (schemas, tables, indexes, ...) for the database in the standby universe
    ```

1. If bootstrapping is required, perform a full copy of the database(s) on the Primary to the Standby using distributed [backup](../../../../reference/configuration/yugabyted/#backup) and [restore](../../../../reference/configuration/yugabyted/#restore). If your source database is not empty or you are using automatic mode, it must be bootstrapped.

1. Enable [point in time restore (PITR)](../../../../manage/backup-restore/point-in-time-recovery/) on the database(s) on both the Primary and Standby universes:

    ```sh
    ./bin/yb-admin \
        --master_addresses <standby_master_addresses> \
        create_snapshot_schedule 1 10 ysql.yugabyte
    ```

1. Set up the database using the checkpoint.

    ```sh
    ./bin/yb-admin \
    --master_addresses <primary_master_addresses> \
    add_namespace_to_xcluster_replication <replication_group_id> <namespace_name> <standby_master_addresses>
    ```

    You should see output similar to the following:

    ```output
    Successfully added db2 to xCluster Replication group repl_group1
    ```

  </div>
</div>

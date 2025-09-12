<!--
+++
private = true
block_indexing = true
+++
-->

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-remove-db" class="nav-link active" id="yugabyted-remove-db-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-remove-db" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      yugabyted
    </a>
  </li>
  <li>
    <a href="#local-remove-db" class="nav-link" id="local-remove-db-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-remove-db" aria-selected="false">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
</ul>

To remove a database from a replication group, use the following command:

<div class="tab-content">
  <div id="yugabyted-remove-db" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-remove-db-tab">

```sh
./bin/yugabyted xcluster remove_database_from_replication \
    --databases <comma_separated_database_names> \
    --replication_id <replication_id> \
    --target_address <ip_of_any_target_cluster_node>
```

  </div>

  <div id="local-remove-db" class="tab-pane fade " role="tabpanel" aria-labelledby="local-remove-db-tab">

```sh
./bin/yb-admin \
-master_addresses <primary_master_addresses> \
remove_namespace_from_xcluster_replication <replication_group_id> <namespace_name> <standby_master_addresses>
```

You should see output similar to the following:

```output
Successfully removed db2 from xCluster Replication group repl_group1
```

  </div>
</div>

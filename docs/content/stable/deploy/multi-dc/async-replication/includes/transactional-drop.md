<!--
+++
private = true
block_indexing = true
+++
-->

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-drop" class="nav-link active" id="yugabyted-drop-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-drop" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      yugabyted
    </a>
  </li>
  <li>
    <a href="#local-drop" class="nav-link" id="local-drop-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-drop" aria-selected="false">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted-drop" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-drop-tab">

To drop a replication group, use the following command:

```sh
./bin/yugabyted xcluster delete_replication \
    --replication_id <replication_id> \
    --target_address <ip_of_any_target_cluster_node>
```

  </div>

  <div id="local-drop" class="tab-pane fade " role="tabpanel" aria-labelledby="local-drop-tab">

To drop a replication group, use the following command:

```sh
./bin/yb-admin \
-master_addresses <primary_master_addresses> \
drop_xcluster_replication <replication_group_id> <standby_master_addresses>
```

You should see output similar to the following:

```output
Outbound xCluster Replication group rg1 deleted successfully
```

  </div>
</div>

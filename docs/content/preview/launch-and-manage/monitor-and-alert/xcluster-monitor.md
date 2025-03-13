---
title: Monitor xCluster
headerTitle: Monitor xCluster
linkTitle: xCluster
description: Monitoring the health of xCluster replication
headContent: Monitor xCluster
menu:
  preview:
    parent: monitor-and-alert
    identifier: xcluster-monitor
    weight: 110
type: docs
---


## Metrics

The list of xCluster metrics is available in the [Metrics Reference](../metrics/replication/).

## Console

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

<!-- YugabyteD Monitor -->

Use the sub-command [xcluster status](../../../reference/configuration/yugabyted/#status-1) to display information about the specified xCluster replications. This command can be run on either the source or target cluster.

```sh
./bin/yugabyted xcluster status
```

To display the status of a specific xCluster replication, run the following command:

```sh
./bin/yugabyted xcluster status --replication_id <replication_id>
```

Example output:
```output
Outbound xCluster Replications:

+------------------------------------------------------------------------------------------+
|                                        yugabyted                                         |
+------------------------------------------------------------------------------------------+
| Replication 1               :                                                            |
| Replication ID              : rg1                                                        |
| State                       : REPLICATING                                                |
| Target Universe UUID        : 2e3ff9c4-2c5a-4dc6-a6bc-20e2548b9f09                       |
| Databases                   : Following are the databases included in this replication   |
|                             : Database 1:                                                |
|                             : Name: yugabyte                                             |
|                             : State: READY                                               |
+------------------------------------------------------------------------------------------+

No Inbound xCluster replications found for this cluster.

```

  </div>

  <div id="local" class="tab-pane fade " role="tabpanel" aria-labelledby="local-tab">

<!-- Local Monitor -->

To list outgoing groups on the Primary universe, use the [list_xcluster_outbound_replication_groups](../../../admin/yb-admin/#list_xcluster_outbound_replication_groups) command:

```sh
./bin/yb-admin \
-master_addresses <primary_master_addresses> \
list_xcluster_outbound_replication_groups [namespace_id]
```

To list inbound groups on the Standby universe, use the [list_universe_replications](../../../admin/yb-admin/#list_universe_replications) command:

```sh
./bin/yb-admin \
-master_addresses <standby_master_addresses> \
list_universe_replications [namespace_id]
```

To get the status of the replication group use [get_replication_status](../../../admin/yb-admin/#get-replication-status)

```sh
yb-admin \
    -master_addresses <target-master-addresses> \
    get_replication_status [ <source_universe_uuid>_<replication_name> ]
```

  </div>

</div>

## xCluster safe time

For transactional xCluster replication groups, use the [get_xcluster_safe_time](../../../admin/yb-admin/#get-xcluster-safe-time) command to see the current xCluster safe time:

```sh
yb-admin \
    -master_addresses <standby_master_addresses> \
    get_xcluster_safe_time \
    [include_lag_and_skew]
```

## YB-Master and YB-Tserver UI

You can access the YB-Master and YB-Tserver UIs to monitor the health of xCluster replication at `/xcluster`.

*YB-Master outbound*

<http://127.0.0.1:7000/xcluster>
![Source YB-Master outbound](/images/deploy/xcluster/automatic-outbound.jpg)

*YB-Master inbound*

<http://127.0.0.1:7000/xcluster>:
![Target YB-Master inbound](/images/deploy/xcluster/automatic-inbound.jpg)

*YB-Tserver outbound*

<http://127.0.0.1:9000/xcluster>:
![Source YB-Tserver inbound](/images/deploy/xcluster/tserver-outbound.jpg)

## Advanced troubleshooting

For advanced troubleshooting, refer to the [Troubleshooting xCluster Replication](https://support.yugabyte.com/hc/en-us/articles/29809348650381-How-to-troubleshoot-xCluster-replication-lag-and-errors) guide.
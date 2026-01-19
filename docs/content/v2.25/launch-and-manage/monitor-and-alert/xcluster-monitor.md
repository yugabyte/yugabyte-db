---
title: Monitor xCluster
headerTitle: Monitor xCluster
linkTitle: xCluster
description: Monitoring the health of xCluster replication
headContent: Monitoring the state and health of xCluster replication
menu:
  v2.25:
    parent: monitor-and-alert
    identifier: xcluster-monitor
    weight: 110
type: docs
---

## Metrics

The list of xCluster metrics is available in the [xCluster metrics](../metrics/replication/).

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

Use the [xcluster status](../../../reference/configuration/yugabyted/#status-1) sub command to display information about the specified xCluster replication. You can run the command on either the source or target cluster.

```sh
./bin/yugabyted xcluster status \
  [--replication_id <replication_id>]
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

To list outgoing groups on the Primary universe, use the [list_xcluster_outbound_replication_groups](../../../admin/yb-admin/#list-xcluster-outbound-replication-groups) command:

```sh
./bin/yb-admin \
    --master_addresses <primary_master_addresses> \
    list_xcluster_outbound_replication_groups \
    [namespace_id]
```

To list inbound groups on the Standby universe, use the [list_universe_replications](../../../admin/yb-admin/#list-universe-replications) command:

```sh
./bin/yb-admin \
    --master_addresses <standby_master_addresses> \
    list_universe_replications \
    [namespace_id]
```

To get the status of the replication group, use [get_replication_status](../../../admin/yb-admin/#get-replication-status):

```sh
yb-admin \
    --master_addresses <target-master-addresses> \
    get_replication_status \
    [<replication_id>]
```

  </div>

</div>

## xCluster safe time

In transactional xCluster replication setups, the current xCluster safe time is the safe time as of which consistent reads are performed on the target universe. You can use the following commands to see the current xCluster safe time:

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted-safe-time" class="nav-link active" id="yugabyted-safe-time-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted-safe-time" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>
  <li>
    <a href="#local-safe-time" class="nav-link" id="local-safe-time-tab" data-bs-toggle="tab"
      role="tab" aria-controls="local-safe-time" aria-selected="false">
      <i class="icon-shell"></i>
      Local
    </a>
  </li>
</ul>
<div class="tab-content">
  <div id="yugabyted-safe-time" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-safe-time-tab">

```sh
./bin/yugabyted xcluster status \
    [--replication_id <replication_id>]
```

Example output:

```output
No Outbound xCluster replications found for this cluster.
Inbound xCluster Replications:

+------------------------------------------------------------------------------------------+
|                                        yugabyted                                         |
+------------------------------------------------------------------------------------------+
| Replication 1               :                                                            |
| Replication ID              : rg1                                                        |
| State                       : ACTIVE                                                     |
| Source cluster nodes        : 127.0.0.1,127.0.0.2,127.0.0.3                              |
| Databases                   : Following are the databases included in this replication   |
|                             : Database 1:                                                |
|                             : Name: yugabyte                                             |
|                             : Safe Time: 2025-03-14 20:43:49.723305                      |
|                             : Safe Time Lag(micro secs): 0.88                            |
|                             : Safe Time Skew(micro secs): 0.81                           |
+------------------------------------------------------------------------------------------+
```

  </div>

  <div id="local-safe-time" class="tab-pane fade " role="tabpanel" aria-labelledby="local-safe-time-tab">

  ```sh
  yb-admin \
      --master_addresses <standby_master_addresses> \
      get_xcluster_safe_time \
      [include_lag_and_skew]
  ```

  Example output:

  ```output
  [
      {
          "namespace_id": "000034cb000030008000000000000000",
          "namespace_name": "yugabyte",
          "safe_time": "2025-03-14 20:51:25.915918",
          "safe_time_epoch": "1742010685915918",
          "safe_time_lag_sec": "0.50",
          "safe_time_skew_sec": "0.02"
      }
  ]
  ```

  </div>
  </div>

## YB-Master and YB-TServer UI

You can access the YB-Master and YB-TServer UIs to monitor the health of xCluster replication at `/xcluster`.

**YB-Master source**

<http://127.0.0.1:7000/xcluster>
![Source YB-Master outbound](/images/deploy/xcluster/automatic-outbound.jpg)

**YB-Master target**

<http://127.0.0.1:7000/xcluster>:
![Target YB-Master inbound](/images/deploy/xcluster/automatic-inbound.jpg)

**YB-TServer source**

<http://127.0.0.1:9000/xcluster>:
![Source YB-TServer inbound](/images/deploy/xcluster/tserver-outbound.jpg)

**YB-TServer target**

<http://127.0.0.1:9000/xcluster>

## Advanced troubleshooting

For advanced troubleshooting, refer to the [Troubleshooting xCluster Replication](https://support.yugabyte.com/hc/en-us/articles/29809348650381-How-to-troubleshoot-xCluster-replication-lag-and-errors) guide.

---
title: Read replica clusters
linkTitle: Read replica clusters
description: Read replica clusters
menu:
  latest:
    parent: multi-dc
    identifier: read-replica-clusters
    weight: 634
type: page
isTocNested: true
showAsideToc: true
---

Read replicas are _observer nodes_ that do not participate in writes, but get a timeline-consistent copy of the data through asynchronous replication. These "read-only" nodes allow your data to be stored safely and durably in a read replica cluster that is 




For details on creating read replica clusters on the Yugabyte Platform, see [Read relicas](../../../manage/enterprise-edition/read-replicas/)

## Create read replicas cluster

1. Start the master processes and let them form a quorum.
2. Define the placement information for the primary cluster.

    ```sh
    $ ./bin/yb-admin modify_read_replica_placement_info <placement_info> <replication_factor>     [placement_uuid]
    ```

    - *placement_info*: Comma separated list of zones in format <cloud1.region1.zone1>,<cloud2.region2.    zone2>,etc…
    - *replication_factor*: Replication factor of the primary cluster.
    - *placement_uuid*: any human readable string representing primary cluster ID.

3. Define the placement information for the read replica cluster.

    ```sh
    $ ./bin/yb-admin add_read_replica_placement_info <placement_info> <replication_factor> [placement_uuid]
    ```

    - *placement_info*: Comma-separated list of availability zones in format `cloud1.region1.    zone1`:<num_replicas_in_zone1>,cloud2.region2.zone2:<num_replicas_in_zone_2>,etc... Note that these  zones should be distinct from the live zones from step 2 (if you want to use the same cloud, region, and zone as a live cluster, one option is to suffix the zone with _rr: for example, “c1.r1.z1” vs c1.r1.z1_rr”).
    - *replication_factor*: The total number of read replica clusters.
    - *placement_uuid*: A human-readable string representing read replica cluster ID (`uuid`).

4. Start the primary `yb-tserver` services with the following configuration options (flags).

   - [--placement_cloud *placement_cloud*](../../../reference/configuration/yb-tserver/#placement-cloud)
   - [--placement_region *placement_region*](../../../reference/configuration/yb-tserver/#placement-region)
   - [--placement_zone *placement_zone*](../../../reference/configuration/yb-tserver/#placement-zone)
   - [--placement_uuid *live_id*](../../../reference/configuration/yb-tserver/#placement-uuid)

    **Note:** The placements should match the information in step 2.

5. Start the read replica `yb-tserver` services with the following configuration options (flags):

   - [--placement_cloud *placement_cloud*](../../../reference/configuration/yb-tserver/#placement-cloud)
   - [--placement_region *placement_region*](../../../reference/configuration/yb-tserver/#placement-region)
   - [--placement_zone *placement_zone*](../../../reference/configuration/yb-tserver/#placement-zone)
   - [--placement_uuid *read_replica_id*](../../../reference/configuration/yb-tserver/#placement-uuid)

    **Note:** The placements should match the information in step 3.

You now have a cluster running with both a primary placement and read-replica placement.

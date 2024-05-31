---
title: Deploy read replica clusters
headerTitle: Read replica deployment
linkTitle: Read replicas
description: Deploy read replica clusters to asynchronously replicate data from the primary cluster and guarantee timeline consistency.
headContent: Deploy read replicas to asynchronously replicate data to different regions
menu:
  stable:
    parent: multi-dc
    identifier: read-replica-clusters
    weight: 620
type: docs
---

In a YugabyteDB deployment, replication of data between nodes of your primary cluster runs synchronously and guarantees strong consistency. Optionally, you can create a read replica cluster that asynchronously replicates data from the primary cluster and guarantees timeline consistency (with bounded staleness). A synchronously replicated primary cluster can accept writes to the system. Using a read replica cluster allows applications to serve low latency reads in remote regions.

In a read replica cluster, read replicas are *observer nodes* that do not participate in writes, but get a timeline-consistent copy of the data through asynchronous replication from the primary cluster.

This document describes how to deploy a read replica cluster using YugabyteDB. For information on deploying read replica clusters using YugabyteDB Anywhere, see [Create a read replica cluster](../../../yugabyte-platform/create-deployments/read-replicas/).

## Deploy a read replica cluster

You can deploy a read replica cluster that asynchronously replicates data with a primary cluster as follows:

1. Start the primary `yb-master` services and let them form a quorum.

1. Define the primary cluster placement using the [`yb-admin modify_placement_info`](../../../admin/yb-admin/#modify-placement-info) command, as follows:

    ```sh
    ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 modify_placement_info <placement_info> <replication_factor> [placement_uuid]
    ```

    - *placement_info*: Comma-separated list of availability zones using the format `<cloud1.region1.zone1>,<cloud2.region2.zone2>, ...`
    - *replication_factor*: Replication factor (RF) of the primary cluster.
    - *placement_uuid*: The placement identifier for the primary cluster, using a meaningful string.

1. Define the read replica placement using the [`yb-admin add_read_replica_placement_info`](../../../admin/yb-admin/#add-read-replica-placement-info) command, as follows:

    ```sh
    ./bin/yb-admin -master_addresses ip1:7100,ip2:7100,ip3:7100 add_read_replica_placement_info <placement_info> <replication_factor> [placement_uuid]
    ```

    - *placement_info*: Comma-separated list of availability zones, using the format `<cloud1.region1.zone1>:<num_replicas_in_zone1>,<cloud2.region2.zone2>:<num_replicas_in_zone2>,...` These read replica availability zones must be uniquely different from the primary availability zones defined in step 2. If you want to use the same cloud, region, and availability zone as a primary cluster, one option is to suffix the zone with `_rr` (for read replica). For example, `c1.r1.z1_rr:2`.
    - *replication_factor*: The total number of read replicas.
    - *placement_uuid*: The identifier for the read replica cluster, using a meaningful string.

1. Start the primary `yb-tserver` services, including the following configuration flags:

   - [--placement_cloud *placement_cloud*](../../../reference/configuration/yb-tserver/#placement-cloud)
   - [--placement_region *placement_region*](../../../reference/configuration/yb-tserver/#placement-region)
   - [--placement_zone *placement_zone*](../../../reference/configuration/yb-tserver/#placement-zone)
   - [--placement_uuid *live_id*](../../../reference/configuration/yb-tserver/#placement-uuid)

   The placements should match the information in step 2. You do not need to add these configuration flags to your `yb-master` configurations.

1. Start the read replica `yb-tserver` services, including the following configuration flags:

   - [--placement_cloud *placement_cloud*](../../../reference/configuration/yb-tserver/#placement-cloud)
   - [--placement_region *placement_region*](../../../reference/configuration/yb-tserver/#placement-region)
   - [--placement_zone *placement_zone*](../../../reference/configuration/yb-tserver/#placement-zone)
   - [--placement_uuid *read_replica_id*](../../../reference/configuration/yb-tserver/#placement-uuid)

   The placements should match the information in step 3.

The primary cluster should begin asynchronous replication with the read replica cluster.

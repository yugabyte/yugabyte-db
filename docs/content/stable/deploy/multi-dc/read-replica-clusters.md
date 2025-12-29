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

{{< page-finder/head text="Deploy read replicas" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" current="" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" url="../../../yugabyte-platform/create-deployments/read-replicas/" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/stable/yugabyte-cloud/cloud-clusters/managed-read-replica/" >}}
{{< /page-finder/head >}}

In a YugabyteDB deployment, replication of data between nodes of your primary cluster runs synchronously and guarantees strong consistency. Optionally, you can create a read replica cluster that asynchronously replicates data from the primary cluster and guarantees timeline consistency (with bounded staleness). A synchronously replicated primary cluster can accept writes to the system. Using a read replica cluster allows applications to serve low latency reads in remote regions.

In a read replica cluster, read replicas are *observer nodes* that do not participate in writes, but get a timeline-consistent copy of the data through asynchronous replication from the primary cluster.

## Deploy a read replica cluster

<ul class="nav nav-tabs-alt nav-tabs-yb custom-tabs">
  <li>
    <a href="#yugabyted" class="nav-link active" id="yugabyted-tab" data-bs-toggle="tab"
      role="tab" aria-controls="yugabyted" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      yugabyted
    </a>
  </li>
  <li>
    <a href="#manual" class="nav-link" id="manual-tab" data-bs-toggle="tab"
      role="tab" aria-controls="manual" aria-selected="false">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="yugabyted" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-tab">

To create a read replica cluster, you first create a YugabyteDB cluster; this example assumes the primary cluster is deployed. Refer to [Deploy](../../manual-deployment/start-yugabyted/).

You add read replica nodes to the primary cluster using the `--join` and `--read_replica` flags.

To create a secure read replica cluster, first [create your VMs](../../manual-deployment/system-config/) and [install YugabyteDB](../../manual-deployment/install-software/).

Then, on the machine running your primary cluster, generate the certificates for each read replica node, passing in the IP addresses of the read replica nodes.

```sh
./bin/yugabyted cert generate_server_certs --hostnames=<IP_of_RR_1>,<IP_of_RR_2>,<IP_of_RR_3>,<IP_of_RR_4>,<IP_of_RR_5>
```

The certificates are added to directories named `$HOME/var/generated_certs/<IP_Address>/`, one for each node certificate you generated.

Copy the certificates to the respective read replica nodes in the `<base_dir>/certs` directory.

To create the read replica cluster, do the following:

1. Add read replica nodes on separate VMs using the `--join` and `--read_replica` flags, as follows:

    ```sh
    ./bin/yugabyted start \
        --secure \
        --advertise_address=<IP_of_RR_1> \
        --join=<IP_of_VM_1> \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node4 \
        --cloud_location=aws.us-east-1.us-east-1d \
        --read_replica

    ./bin/yugabyted start \
        --secure \
        --advertise_address=<IP_of_RR_2> \
        --join=<IP_of_VM_1> \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node5 \
        --cloud_location=aws.us-east-1.us-east-1d \
        --read_replica

    ./bin/yugabyted start \
        --secure \
        --advertise_address=<IP_of_RR_3> \
        --join=<IP_of_VM_1> \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node6 \
        --cloud_location=aws.us-east-1.us-east-1e \
        --read_replica

    ./bin/yugabyted start \
        --secure \
        --advertise_address=<IP_of_RR_4> \
        --join=<IP_of_VM_1> \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node7 \
        --cloud_location=aws.us-east-1.us-east-1f \
        --read_replica

    ./bin/yugabyted start \
        --secure \
        --advertise_address=<IP_of_RR_5> \
        --join=<IP_of_VM_1> \
        --base_dir=$HOME/yugabyte-{{< yb-version version="stable" >}}/node8 \
        --cloud_location=aws.us-east-1.us-east-1f \
        --read_replica
    ```

  </div>

  <div id="manual" class="tab-pane fade" role="tabpanel" aria-labelledby="manual-tab">

You can deploy a read replica cluster that asynchronously replicates data with a primary cluster as follows:

1. Start the primary yb-master services and let them form a quorum.

1. Define the primary cluster placement using the [`yb-admin modify_placement_info`](../../../admin/yb-admin/#modify-placement-info) command, as follows:

    ```sh
    ./bin/yb-admin --master_addresses ip1:7100,ip2:7100,ip3:7100 modify_placement_info <placement_info> <replication_factor> [placement_uuid]
    ```

    - *placement_info*: Comma-separated list of availability zones using the format `<cloud1.region1.zone1>,<cloud2.region2.zone2>, ...`
    - *replication_factor*: Replication factor (RF) of the primary cluster.
    - *placement_uuid*: The placement identifier for the primary cluster, using a meaningful string.

1. Define the read replica placement using the [`yb-admin add_read_replica_placement_info`](../../../admin/yb-admin/#add-read-replica-placement-info) command, as follows:

    ```sh
    ./bin/yb-admin --master_addresses ip1:7100,ip2:7100,ip3:7100 add_read_replica_placement_info <placement_info> <replication_factor> [placement_uuid]
    ```

    - *placement_info*: Comma-separated list of availability zones, using the format `<cloud1.region1.zone1>:<num_replicas_in_zone1>,<cloud2.region2.zone2>:<num_replicas_in_zone2>,...` These read replica availability zones must be uniquely different from the primary availability zones defined in step 2. If you want to use the same cloud, region, and availability zone as a primary cluster, one option is to suffix the zone with `_rr` (for read replica). For example, `c1.r1.z1_rr:2`.
    - *replication_factor*: The total number of read replicas.
    - *placement_uuid*: The identifier for the read replica cluster, using a meaningful string.

1. Start the primary yb-tserver services, including the following configuration flags:

   - [--placement_cloud *placement_cloud*](../../../reference/configuration/yb-tserver/#placement-cloud)
   - [--placement_region *placement_region*](../../../reference/configuration/yb-tserver/#placement-region)
   - [--placement_zone *placement_zone*](../../../reference/configuration/yb-tserver/#placement-zone)
   - [--placement_uuid *live_id*](../../../reference/configuration/yb-tserver/#placement-uuid)

   The placements should match the information in step 2. You do not need to add these configuration flags to your yb-master configurations.

1. Start the read replica yb-tserver services, including the following configuration flags:

   - [--placement_cloud *placement_cloud*](../../../reference/configuration/yb-tserver/#placement-cloud)
   - [--placement_region *placement_region*](../../../reference/configuration/yb-tserver/#placement-region)
   - [--placement_zone *placement_zone*](../../../reference/configuration/yb-tserver/#placement-zone)
   - [--placement_uuid *read_replica_id*](../../../reference/configuration/yb-tserver/#placement-uuid)

   The placements should match the information in step 3.

The primary cluster should begin asynchronous replication with the read replica cluster.

  </div>

</div>

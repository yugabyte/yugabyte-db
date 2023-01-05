---
title: Synchronous replication (3+ regions)
headerTitle: Synchronous replication (3+ regions)
linkTitle: Synchronous (3+ regions)
description: Global data distributed using synchronous replication across regions.
headcontent: Distribute data across regions
aliases:
  - /preview/explore/global-distribution-linux/
  - /preview/explore/global-distribution/macos
  - /preview/explore/global-distribution/linux
menu:
  preview:
    identifier: explore-multi-region-deployments-sync-replication-1-ysql
    parent: explore-multi-region-deployments
    weight: 710
type: docs
---

YugabyteDB can be deployed in a globally distributed manner to serve application queries from the region closest to the end users with low latencies as well as to survive any outages to ensure high availability.

This page simulates AWS regions on a local machine. First, you deploy YugabyteDB in the `us-west-2` region across multiple availability zones (`a`, `b`, `c`) and start a workload against this cluster. Next, you change the topology to run across multiple geographic regions in US East (`us-east-1`) and Tokyo (`ap-northeast-1`), with the workload running uninterrupted during the entire transition.

{{< note title="Setup for POCs" >}}

The steps can also be used for deploying clusters in any public cloud, private data center, or in separate VMs. The only differences are as follows:

- You don't need to specify the `--advertise_address` flag.
- You don't need to configure loopback addresses.
- Replace the IP addresses in the commands with the corresponding IP addresses of your nodes.

{{< /note >}}

## Create a multi-zone cluster in US West

If you have a previously running local cluster, destroy it.

Start a 3-node cluster with a replication factor (RF) of `3`, and each replica placed in different zones (`us-west-2a`, `us-west-2b`, `us-west-2c`) in the `us-west-2` (Oregon) region of AWS.

Start by first creating a single node cluster as follows:

```sh
./bin/yugabyted start \
                --advertise_address 127.0.0.1 \
                --cloud_location aws.us-west-2.us-west-2a \
                --fault_tolerance zone
```

When creating a local cluster on MacOS and Linux, the additional nodes need loopback addresses configured:

```sh
sudo ifconfig lo0 alias 127.0.0.2
sudo ifconfig lo0 alias 127.0.0.3
```

Next, join two more nodes with the previous node. By default, [yugabyted](../reference/configuration/yugabyted/) creates a cluster with a replication factor of `3` when the third node is added.

```sh
./bin/yugabyted start \
                --advertise_address 127.0.0.2 \
                --cloud_location aws.us-west-2.us-west-2b \
                --fault_tolerance zone \
                --join node1-ip-address
```

```sh
./bin/yugabyted start \
                --advertise_address 127.0.0.3 \
                --cloud_location aws.us-west-2.us-west-2c \
                --fault_tolerance zone \
                --join node1-ip-address
```

After starting the yugabyted processes on all the nodes, configure the data placement constraint of the cluster as follows:

```sh
./bin/yugabyted configure data_placement --fault_tolerance=zone
```

The [configure](../../../reference/configuration/yugabyted/#configure) command determines the data placement constraint based on the `--cloud_location` of each node in the cluster. If three or more zones are available in the cluster, `configure` configures the cluster to survive at least one availability zone failure. Otherwise, it outputs a warning message. The command can be executed on any node where you already started YugabyteDB.

## Review the deployment

In this deployment, the YB-Masters are each placed in a separate zone to allow them to survive the loss of a zone. You can view the masters on the [dashboard](http://localhost:7000/), as per the following illustration:

![Multi-zone cluster YB-Masters](/images/ce/online-reconfig-multi-zone-masters.png)

You can view the tablet servers on the [tablet servers page](http://localhost:7000/tablet-servers), as per the following illustration:

![Multi-zone cluster YB-TServers](/images/ce/online-reconfig-multi-zone-tservers.png)

## Start a workload

Follow the [setup instructions](../../#set-up-yb-workload-simulator) to connect the YB Workload Simulator application, and run a read-write workload. To verify that the application is running correctly, navigate to the application UI at <http://localhost:8080/> to view the cluster network diagram and Latency and Throughput charts for the running workload.

You should now see some read and write load on the [tablet servers page](http://localhost:7000/tablet-servers), as per the following illustration:

![Multi-zone cluster load](/images/ce/online-reconfig-multi-zone-load.png)

## Add nodes

### Add new nodes in US East and Tokyo regions

Add a node in the zone `us-east-1a` of region `us-east-1`, as follows:

```sh
./bin/yb-ctl add_node --placement_info "aws.us-east-1.us-east-1a"
```

Add another node in the zone `ap-northeast-1a` of region `ap-northeast-1`, as follows:

```sh
./bin/yb-ctl add_node --placement_info "aws.ap-northeast-1.ap-northeast-1a"
```

These two new nodes are added into the cluster but are not taking any read or write IO. This is because the YB-Master's initial placement policy of storing data across the zones in `us-west-2` region still applies, as per the following illustration:

![Add node in a new region](/images/ce/online-reconfig-add-regions-no-load.png)

### Update placement policy

Update the placement policy, instructing the YB-Master to place data in the new regions, as follows:

```sh
./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100 \
    modify_placement_info aws.us-west-2.us-west-2a,aws.us-east-1.us-east-1a,aws.ap-northeast-1.ap-northeast-1a 3
```

You should see that the data as well as the IO gradually moves from the nodes in `us-west-2b` and `us-west-2c` to the newly added nodes. The [tablet servers page](http://localhost:7000/tablet-servers) should soon look similar to the following illustration:

![Multi region workload](/images/ce/online-reconfig-multi-region-load.png)

## Retire old nodes

### Start new masters

You need to move the YB-Master from the old nodes to the new nodes. To do so, first start new masters on the new nodes, as follows:

```sh
./bin/yb-ctl add_node --master --placement_info "aws.us-east-1.us-east-1a"
```

```sh
./bin/yb-ctl add_node --master --placement_info "aws.ap-northeast-1.ap-northeast-1a"
```

![Add master](/images/ce/online-reconfig-add-masters.png)

### Remove old masters

Remove the old masters from the masters Raft group. Assuming nodes with addresses `127.0.0.2` and `127.0.0.3` were the two old nodes, run the following commands:

```sh
./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.2:7100,127.0.0.3:7100,127.0.0.4:7100,127.0.0.5:7100 change_master_config REMOVE_SERVER 127.0.0.2 7100
```

```sh
./bin/yb-admin --master_addresses 127.0.0.1:7100,127.0.0.3:7100,127.0.0.4:7100,127.0.0.5:7100 change_master_config REMOVE_SERVER 127.0.0.3 7100
```

![Add master](/images/ce/online-reconfig-remove-masters.png)

### Remove old nodes

Remove the old nodes, as follows:

```sh
./bin/yb-ctl remove_node 2
```

```sh
./bin/yb-ctl remove_node 3
```

![Add master](/images/ce/online-reconfig-remove-nodes.png)

## Clean up

Optionally, you can shutdown the local cluster created in Step 1, as follows:

```sh
./bin/yb-ctl destroy
```

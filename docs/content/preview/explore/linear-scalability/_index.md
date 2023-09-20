---
title: Horizontal scalability
headerTitle: Horizontal scalability
linkTitle: Horizontal scalability
description: Horizontal scalability in YugabyteDB.
headcontent: Handle larger workloads by adding nodes to your cluster
aliases:
  - /explore/linear-scalability/
  - /preview/explore/linear-scalability/
  - /preview/explore/postgresql/linear-scalability/
  - /preview/explore/linear-scalability-macos/
  - /preview/explore/linear-scalability/linux/
  - /preview/explore/linear-scalability/docker/
  - /preview/explore/linear-scalability/kubernetes/
  - /preview/explore/auto-sharding/macos/
  - /preview/explore/auto-sharding/linux/
  - /preview/explore/auto-sharding/docker/
  - /preview/explore/auto-sharding/kubernetes/
image: /images/section_icons/explore/linear_scalability.png
menu:
  preview:
    identifier: explore-scalability
    parent: explore
    weight: 220
type: indexpage
---

YugabyteDB can be scaled either horizontally or vertically depending on your needs. YugabyteDB automatically splits user tables into multiple [shards](../../../architecture/docdb-sharding/sharding/?), called tablets. You can either add more nodes to distribute the tablets or increase the specs of your nodes to scale your universe efficiently and reliably to handle the following:

* High transactions per second
* High number of concurrent client connections
* Large datasets

## Horizontal scaling (scale out)

Horizontal scaling, also referred to as scaling out, is the process of adding more nodes to a distributed database to handle increased load and data. In YugabyteDB, data is split (sharded) into tablets, and these multiple tablets are located on each node. When more nodes are added, some tablets are automatically moved to the new nodes. Tablets can be split dynamically as needed to use the newly added resource, which leads to each node managing fewer tablets. The entire cluster can therefore handle more transactions and queries in parallel, thus increasing its capacity to handle larger workloads.

Horizontal scaling is the most common scaling model in YugabyteDB, and has several advantages, including:

* **Improved performance** - More nodes can process requests in parallel, reducing response times.
* **Cost-effectiveness** - You can use commodity hardware, which is generally less expensive than high-end servers.
* **Elastic scaling** - You can add new nodes as needed to accommodate growth or scale-out temporarily to handle high traffic for special events such as Black Friday shopping or a major news outbreak. After the event, you can reduce the size of the cluster (*scale in*) by draining all the data from some of the nodes (or Kubernetes pods) and removing them from the universe.

## Vertical scaling (scale up)

Vertical scaling involves upgrading the existing hardware or resources of each of the nodes in your cluster. Instead of adding more machines, you enhance the capabilities of a single machine by increasing its CPU, memory, storage, and so on. Vertical scaling is often limited by the capacity of a single server and can get expensive as you move to more powerful hardware. Although you retain the same number of nodes, which could simplify your operations, eventually hardware resources reach their limits, and further scaling up might not be feasible.

In some cases, depending on your application needs and budget constraints, a combination of both horizontal and vertical scaling may be used to achieve the desired performance and scalability goals.

## Horizontal vs Vertical Scaling

The following table lists the pros and cons of Horizontal/Vertical scaling of a YugabyteDB cluster.

|                     |           Horizontal Scaling            |                               Vertical Scaling                                |
| ------------------- | --------------------------------------- | ----------------------------------------------------------------------------- |
| Also known as       | Scale out                               | Scale up                                                                      |
| No. of nodes        | Increases                               | Remains the same                                                              |
| Ease of effort      | Add new nodes to the cluster            | Add more powerful nodes, drain the old node, and remove them from the cluster |
| Fault Tolerance     | Increases as more nodes have been added | Remains the same                                                              |
| Cluster rebalancing | Faster                                  | Slower                                                                        |
| Future scaling      | More nodes can be added                 | Limited to the most powerful machines available today                         |
| Added costs         | Cost of newer machines                  | Difference in cost of the new and old machines                                |
| Disk | Same disks as other nodes can be used as data and connections will be distributed | Along with CPU and Memory, disks should also be updgraded to handle increased workloads |

## Learn more

{{<index/block>}}

  {{<index/item
    title="Scaling transactions per second"
    body="Scale out a universe to handle a greater number of concurrent transactions per second."
    href="scaling-transactions-cloud/"
    icon="/images/section_icons/explore/linear_scalability.png">}}

  {{<index/item
    title="Distribute data across nodes"
    body="Automatic data distribution across the universe's nodes using transparent sharding of tables."
    href="sharding-data/"
    icon="fa-solid fa-building">}}

  {{<index/item
    title="Writes"
    body="See how writes scale in YugabyteDB."
    href="scaling-writes/"
    icon="fa-solid fa-pen">}}

  {{<index/item
    title="Transactions"
    body="See how transactions scale in YugabyteDB."
    href="scaling-transactions-bench/"
    icon="/images/section_icons/explore/auto_sharding.png">}}

  {{<index/item
    title="Simple workloads"
    body="See how large simple workloads scale in YugabyteDB."
    href="scaling-simple-workloads/"
    icon="fa-solid fa-truck-ramp-box">}}

  {{<index/item
    title="Large datasets"
    body="See how large datasets scale in YugabyteDB."
    href="scaling-large-datasets/"
    icon="fa-solid fa-weight-hanging">}}

{{</index/block>}}

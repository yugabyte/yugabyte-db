---
title: Horizontal scalability
headerTitle: Horizontal scalability
linkTitle: Horizontal scalability
description: Horizontal scalability in YugabyteDB.
headcontent: Handle larger workloads by adding nodes to your cluster
aliases:
  - /explore/linear-scalability/
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
showRightNav: true
type: indexpage
---

In YugabyteDB, [data is split (sharded)](./sharding-rebalancing) into tablets, and these multiple tablets are placed on various nodes. When more [nodes are added](./node-addition), some tablets are automatically [rebalanced](./sharding-rebalancing#rebalancing) to the new nodes. Tablets can be split dynamically as needed to use the newly added resource, which leads to each node managing fewer tablets. The entire cluster can therefore handle more transactions and queries in parallel, thus increasing its capacity to handle larger workloads.

You can either add more nodes to distribute the tablets or increase the specifications of your nodes to scale your universe efficiently and reliably to handle the following:

* High transactions per second
* High number of concurrent client connections
* Large datasets
* Data in multiple regions and continents

## Horizontal scaling (scale out)

Horizontal scaling, also referred to as scaling out, is the process of adding more nodes to a distributed database to handle increased load and data. Horizontal scaling is the most common scaling model in YugabyteDB, and has several advantages, including:

* **Improved performance** - More nodes can process requests in parallel, reducing response times.
* **Cost-effectiveness** - You can use commodity hardware, which is generally less expensive than high-end servers.
* **Elastic scaling** - You can add new nodes as needed to accommodate growth or scale-out temporarily to handle high traffic for special events such as Black Friday shopping or a major news outbreak. After the event, you can reduce the size of the cluster (*scale in*) by draining all the data from some of the nodes (or Kubernetes pods) and removing them from the universe.

## Vertical scaling (scale up)

Vertical scaling involves upgrading the existing hardware or resources of each of the nodes in your cluster. Instead of adding more machines, you enhance the capabilities of a single machine by increasing its CPU, memory, storage, and so on. Vertical scaling is often limited by the capacity of a single server and can get expensive as you move to more powerful hardware. Although you retain the same number of nodes, which could simplify your operations, eventually hardware resources reach their limits, and further scaling up might not be feasible.

In some cases, depending on your application needs and budget constraints, a combination of both horizontal and vertical scaling may be used to achieve the desired performance and scalability goals.

## Horizontal vs Vertical scaling

The following table lists the pros and cons of Horizontal and Vertical scaling of a YugabyteDB cluster.

|                         |                           Horizontal Scaling/Scale out                            |                                Vertical Scaling/Scale up                                |
| ----------------------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| **Number of nodes**        | Increases                                                                         | Remains the same                                                                        |
| **Ease of effort**      | Add new nodes to the cluster                                                      | Add more powerful nodes, drain the old node, and remove them from the cluster           |
| **Fault Tolerance**     | Increases as more nodes are added                                           | Remains the same                                                                        |
| **Cluster&nbsp;rebalancing** | Faster                                                                            | Slower                                                                                  |
| **Future scaling**      | More nodes can be added                                                           | Limited to the most powerful machines available currently                                   |
| **Added costs**         | Cost of newer machines                                                            | Difference in cost between the new and old machines                                          |
| **Disk**                | Same disks as other nodes can be used as data and connections will be distributed | Along with CPU and memory, disks should also be upgraded to handle increased workloads |

## Enhancements

[DocDB](../../architecture/docdb/), YugabyteDB's underlying distributed document store, uses a heavily customized version of RocksDB for node-local persistence. It has been engineered from the ground up to deliver high performance at a massive scale. Several features have been built into DocDB to enhance performance, including the following:

* Scan-resistant global block cache
* Bloom/index data splitting
* Global memstore limit
* Separate compaction queues to reduce read amplification
* Smart load balancing across disks

## Learn more

{{<index/block>}}

  {{<index/item
    title="Distribute data across nodes"
    body="Automatic data distribution across a universe's nodes using transparent sharding of tables."
    href="./sharding-rebalancing"
    icon="fa-solid fa-building">}}

  {{<index/item
    title="Scale out by adding nodes"
    body="Seamlessly scale your cluster on demand by adding new nodes to the cluster."
    href="./node-addition"
    icon="fa-solid fa-circle-nodes">}}

  {{<index/item
    title="Reads"
    body="See how reads scale in YugabyteDB."
    href="scaling-reads/"
    icon="fa-brands fa-readme">}}

  {{<index/item
    title="Writes"
    body="See how writes scale in YugabyteDB."
    href="scaling-writes/"
    icon="fa-solid fa-pen">}}

  {{<index/item
    title="Transactions"
    body="See how transactions scale in YugabyteDB."
    href="scaling-transactions/"
    icon="/images/section_icons/explore/auto_sharding.png">}}

  {{<index/item
    title="Large datasets"
    body="See how large datasets scale in YugabyteDB."
    href="scaling-large-datasets/"
    icon="fa-solid fa-weight-hanging">}}

  {{<index/item
    title="Scale out a universe"
    body="Try it out for yourself by following an example."
    href="scaling-universe/"
    icon="fa-solid fa-circle-nodes">}}

{{</index/block>}}

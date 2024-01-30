---
title: Horizontal vs Vertical Scaling
headerTitle: Horizontal vs Vertical Scaling
linkTitle: Horizontal vs Vertical
description: Differences between horizontal and vertical scaling
headcontent: Understand the differences between horizontal and vertical scaling
aliases:
  - /explore/linear-scalability/sharding-rebalancing
menu:
  preview:
    identifier: scalability-horizontal-vs-vertical
    parent: explore-scalability
    weight: 5
rightNav:
  hideH3: true
type: docs
---

After you have decided to scale your system, there are 2 common ways to scale, namely **vertical** and **horizontal**. YugabyteDB supports both. Because YugabyteDB is distributed, scaling is operationally straightforward and performed without any service disruption.

## Vertical (scale up)

This is the standard way to scale traditional databases. It involves upgrading the existing hardware or resources of each of the nodes in your cluster. Instead of adding more machines, you enhance the capabilities of a single machine by increasing its CPU, memory, storage, and so on.

Vertical scaling is often limited by the capacity of a single server and can get expensive as you move to more powerful hardware. Although you retain the same number of nodes, which could simplify your operations, eventually hardware resources reach their limits, and further scaling might not be feasible. In addition, all the data has to be moved, which can take time.

## Horizontal (scale out)

To scale your system horizontally, You [add more nodes](../node-addition/) to handle increased load and data. Horizontal scaling is a first-class feature in YugabyteDB, and is the most common scaling model in YugabyteDB. It has several advantages, including the following:

- Improved performance - More nodes can process requests in parallel, reducing response times. [Reads](../scaling-reads/), [writes](../scaling-writes/), and [transactions](../scaling-transactions/) scale linearly as you add nodes.
- Cost-effective - You can use commodity hardware, which is generally less expensive than high-end servers.
- Elastic - You can add new nodes as needed to handle high traffic for special events, and, when traffic returns to typical levels, scale back in by draining all the data from some nodes (or Kubernetes pods) and removing them from the cluster.
- No limits - There's no practical limit to the number of servers you can add.
- Less disruptive - You don't need to move your application but move only a portion of your data when you add a node.
- Horizontal scaling is also the most practical way to expand to new regions.

## Differences

Depending on your application needs and budget constraints, you can use a combination of both horizontal and vertical scaling to achieve the desired performance and scalability goals. The following table lists the pros and cons of horizontal and vertical scaling of a YugabyteDB cluster.

|                         |                           Horizontal                            |                                Vertical                                |
| ----------------------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| **Number of nodes**     | Increases                                                                         | Remains the same                                                                        |
| **Ease of effort**      | Add new nodes to the cluster                                                      | Add more powerful nodes, drain the old nodes, and remove them from the cluster           |
| **Fault tolerance**     | Increases as more nodes are added                                           | Remains the same                                                                        |
| **Cluster&nbsp;rebalancing** | Faster                                                                            | Slower                                                                                  |
| **Future scaling**      | More nodes can be always be added                                                      | Limited to the most powerful machines available currently                                   |
| **Cost**                | Cost of newer machines                                                            | Difference in cost between the new and old machines                                          |
| **Disk**                | Same disks as other nodes can be used as data and connections will be distributed | Along with CPU and memory, disks should also be upgraded to handle increased workloads |

Ultimately, the choice between horizontal and vertical scaling depends on the specific requirements, architecture, and constraints of the application or system in question. In some cases, a combination of both vertical and horizontal scaling, known as "elastic scaling," may be employed to achieve the desired balance between performance, scalability, and cost efficiency.

## Learn More

- [Data distribution](../data-distribution)
- [Add nodes to a cluster](../node-addition)
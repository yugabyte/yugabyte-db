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

## Why you need to be able to scale seamlessly

Depending on your business, you may find you need to scale for a variety of reasons:

- Growing user base. Your application becomes popular, users love your app, and the user base is growing. Or you might need high throughput right from the get go, and need to add capacity quickly.
- Seasonal traffic. Occasionally, you have to handle a lot more transactions per second than usual. Black Friday and Cyber Monday retail traffic, or streaming for special events like the Superbowl or World Cup, for example.
- Growing datasets. For example, you have an IoT app or an audit database that keeps growing rapidly on a daily basis. These systems have to handle high volume of writes on a regular basis.
- Change in business priorities. Scaling needs are often unpredictable. To take one example, retail priorities shifted radically when Covid entered the picture. With a database that can scale, you can pivot quickly when the business environment shifts.
- New geographies. Your user base grows more geographically diverse, expanding to new regions, and you need to add to your presence globally by adding more data centers in different continents.

Given this fluctuating reality and the always-on expectations of users, being able to scale seamlessly is as important as being able to scale. Scaling needs to be:

- Easy. Scaling should be operationally simple to do.
- Elastic. You should be able to scale up and down as needed.
- Transparent. Your applications shouldn't even know it's happening. Data is moved automatically, without any interruption in service.

## How scaling works

In YugabyteDB, [data is split (sharded)](./sharding-rebalancing) into tablets, and these multiple tablets are placed on various nodes. When more [nodes are added](./node-addition), some tablets are automatically [rebalanced](./sharding-rebalancing#rebalancing) to the new nodes. Tablets can be split dynamically as needed to use the newly added resource, which leads to each node managing fewer tablets. The entire cluster can therefore handle more transactions and queries in parallel, thus increasing its capacity to handle larger workloads.

To learn more, see [Sharding and rebalancing](sharding-rebalancing/).

## How to scale

Depending on your specific requirements, you scale your database to support one or more of the following:

- More transactions per second
- More concurrent client connections
- Larger datasets
- Additional regions for an expanding global presence

You can achieve this in two ways:

- Vertical scaling (scale up)

  Vertical scaling is the standard way to scale traditional databases, and involves upgrading the existing hardware or resources of each of the nodes in your cluster. Instead of adding more machines, you enhance the capabilities of a single machine by increasing its CPU, memory, storage, and so on.

  Vertical scaling is often limited by the capacity of a single server and can get expensive as you move to more powerful hardware. Although you retain the same number of nodes, which could simplify your operations, eventually hardware resources reach their limits, and further scaling up might not be feasible.

- Horizontal scaling (scale out)

  Horizontal scaling is the process of [adding more nodes](./node-addition/) to a distributed database to handle increased load and data. Horizontal scaling is the most common scaling model in YugabyteDB, and has several advantages, including:

  - Improved performance - More nodes can process requests in parallel, reducing response times. [Reads](./scaling-reads/), [writes](./scaling-writes/), and [transactions](./scaling-transactions/) scale linearly as you add nodes.
  - Cost-effectiveness - You can use commodity hardware, which is generally less expensive than high-end servers.
  - Elastic scaling - You can add new nodes as needed to accommodate growth or scale-out temporarily to handle high traffic for special events such as Black Friday shopping or a major news outbreak. After the event, you can reduce the size of the cluster (*scale in*) by draining all the data from some of the nodes (or Kubernetes pods) and removing them from the cluster.
  - Limitless scalability - There's no practical limit to the number of servers you can add.
  - Less disruptive - You don't need to move your application, and only need to move a portion of your data when you add a node.

  Horizontal scaling is also the most practical way to expand to new regions.

YugabyteDB supports both vertical and horizontal scaling. Because YugabyteDB is distributed, scaling is operationally straightforward and performed without any service disruption.

Depending on your application needs and budget constraints, you can use a combination of both horizontal and vertical scaling to achieve the desired performance and scalability goals. The following table lists the pros and cons of horizontal and vertical scaling of a YugabyteDB cluster.

|                         |                           Horizontal Scaling/Scale out                            |                                Vertical Scaling/Scale up                                |
| ----------------------- | --------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------- |
| **Number of nodes**     | Increases                                                                         | Remains the same                                                                        |
| **Ease of effort**      | Add new nodes to the cluster                                                      | Add more powerful nodes, drain the old nodes, and remove them from the cluster           |
| **Fault tolerance**     | Increases as more nodes are added                                           | Remains the same                                                                        |
| **Cluster&nbsp;rebalancing** | Faster                                                                            | Slower                                                                                  |
| **Future scaling**      | More nodes can be always be added                                                      | Limited to the most powerful machines available currently                                   |
| **Added costs**         | Cost of newer machines                                                            | Difference in cost between the new and old machines                                          |
| **Disk**                | Same disks as other nodes can be used as data and connections will be distributed | Along with CPU and memory, disks should also be upgraded to handle increased workloads |

## When to scale

To know when to scale, monitor metrics provided for CPU, memory, and disk space, and set up alerts on these metrics to give you ample time to plan and react.

For best results, keep steady state resource usage under 60%, and take strong action at 75%, in particular for disk space. If CPU or memory is high, the system will slow, but if your disk usage approaches limits, it will also increase usage in followers, and moving and recovering data takes time.

[YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/) and [YugabyteDB Managed](../../yugabyte-cloud/cloud-monitor/) both include metrics dashboards and configurable alerts to keep you notified of changes.

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

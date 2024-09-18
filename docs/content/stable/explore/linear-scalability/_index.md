---
title: Horizontal scalability
headerTitle: Horizontal scalability
linkTitle: Horizontal scalability
description: Horizontal scalability in YugabyteDB.
headcontent: Handle larger workloads by adding nodes to your cluster
image: /images/section_icons/explore/linear_scalability.png
menu:
  stable:
    identifier: explore-scalability
    parent: explore
    weight: 220
showRightNav: true
type: indexpage
---

Being able to scale a distributed system is essential to reliably and efficiently meeting the increasing demands of users, workloads, and data. Scalability is central the design and maintenance of distributed systems. You need it to ensure systems can handle increasing workloads, provide high availability, optimize resource usage, adapt to changing requirements, and accommodate future growth.

Depending on your business, you may need to scale for a variety of reasons:

- **Growing user base**. Your application becomes popular, users love your app, and the user base is expanding.
- **Seasonal traffic**. Occasionally, you have to handle a lot more transactions per second than usual. Black Friday and Cyber Monday retail traffic, or streaming for special events like the Superbowl or World Cup, for example.
- **Growing datasets**. For example, you have an IoT app or an audit database that keeps growing rapidly daily. These systems have to handle a high volume of writes regularly.
- **Changing business priorities**. Scaling needs are often unpredictable. To take one example, retail priorities shifted radically when Covid entered the picture. With a database that can scale, you can pivot quickly when the business environment shifts.
- **New geographies**. Your user base expands to new regions, and you need to add to your presence globally by adding more data centers in different continents.

Being able to scale seamlessly is as important as being able to scale. Scaling needs to be operationally simple and completely transparent to the applications. With YugabyteDB, you can start small and add nodes as needed. You can scale your data, reads, and writes without disrupting ongoing applications. As your needs grow, YugabyteDB automatically shards data and scales out. You can also scale up your cluster for short-term needs and then scale down after the need is over.

## Ways to scale

There are 2 common ways to scale, namely **vertical** and **horizontal**. YugabyteDB supports both. In vertical scaling, you enhance the capabilities of your existing nodes by increasing CPU, memory, storage, and so on. With horizontal scaling, you add more nodes of the same type to your cluster. Horizontal scaling is the most common type of scaling in YugabyteDB. As YugabyteDB is distributed, scaling is operationally straightforward and performed without any service disruption.

{{<lead link="./horizontal-vs-vertical-scaling">}}
To learn more about the pros and cons of the two types of scaling, see [Horizontal vs vertical scaling](./horizontal-vs-vertical-scaling).
{{</lead>}}

## How scaling works

To get a better idea of the effort and time you will need to scale your systems, it's helpful to understand a few basic concepts that describe how YugabyteDB scales. Let's go over them quickly.

### Sharding

Data distribution is critical in scaling. In YugabyteDB, data is [split (sharded)](./data-distribution/) into tablets. A tablet is effectively a piece of a table and these tablets are placed on various nodes. The mapping of a row of a table to a tablet is deterministic and the system knows exactly which tablet holds a specific row.

{{<tip>}}
To learn more about the different types of sharding, see [Hash and range sharding](../../architecture/docdb-sharding/sharding/). For an illustration of how tablets are split, see [Tablet splitting](./data-distribution/#tablet-splitting).
{{</tip>}}

### Rebalancing

As your data grows, tablets are split and moved across the different nodes in the cluster to maintain an equal distribution of data across the nodes. This process is known as _Rebalancing_. Data is moved automatically, without any interruption in service.

{{<lead link="./data-distribution/#rebalancing">}}
For an illustration of how tablets are rebalanced, see [Rebalancing](./data-distribution/#rebalancing).
{{</lead>}}

### Adding nodes

When more [nodes are added](./node-addition), some tablets are automatically [rebalanced](./data-distribution/#rebalancing) to the new nodes, and the entire cluster can therefore handle more transactions and queries in parallel, thus increasing its capacity to handle larger workloads.

{{<lead link="node-addition/">}}
For an illustration of what happens when nodes are added to a cluster, see [Adding nodes](node-addition/).
{{</lead>}}

## When to scale

To know when to scale, monitor metrics provided for CPU, memory, and disk space. Set up alerts on these metrics to give you ample time to plan and react.

For best results, keep steady state resource usage under 60%, and take strong action at 75%, in particular for disk space. If CPU or memory is high, the system will slow; if disk usage approaches limits, usage on followers also increases, and moving and recovering data takes time.

[YugabyteDB Anywhere](../../yugabyte-platform/alerts-monitoring/) and [YugabyteDB Aeon](../../yugabyte-cloud/cloud-monitor/) both include metrics dashboards and configurable alerts to keep you notified of changes.

{{<lead link="../observability">}}
To learn more about the various metrics than you can monitor, see [Observability](../observability).
{{</lead>}}

## Learn more

{{<index/block>}}

  {{<index/item
    title="Distribute data across nodes"
    body="Automatic data distribution across a universe's nodes using transparent sharding of tables."
    href="data-distribution/"
    icon="fa-solid fa-building">}}

  {{<index/item
    title="Horizontal vs vertical scaling"
    body="Understand the differences between horizontal and vertical scaling."
    href="horizontal-vs-vertical-scaling/"
    icon="fa-solid fa-circle-nodes">}}

  {{<index/item
    title="Scale out by adding nodes"
    body="Seamlessly scale your cluster on demand by adding new nodes to the cluster."
    href="node-addition/"
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

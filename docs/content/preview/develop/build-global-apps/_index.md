---
title: Build global applications
headerTitle: Build global applications
linkTitle: Build global applications
description: Build globally distributed applications.
headcontent: Learn how to design globally distributed applications using simple patterns
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview:
    identifier: build-global-apps
    parent: develop
    weight: 201
type: indexpage
---

{{<srcdiagram href="https://docs.google.com/presentation/d/1lEajQyVZLhmHRKmBxunf1LucWkQkrJ3rIthoHxZvyQc/edit#slide=id.g22bc5dd47b0_0_18">}}

In today's fast-paced world, the internet and cloud technology have revolutionized the way people interact and operate. Cloud introduces multiple regions leading to data being distributed and replicated over various geographies. A new class of applications must be developed to access and maintain globally distributed data. Let's understand the reasons why today's applications have to be global and look at some patterns that YugabyteDB offers to design your global applications with ease.

## The need for global applications

### Business Continuity and Disaster Recovery

Although public clouds have come a long way since the inception of AWS in 2006, region and zone outages are still fairly common, happening once or twice a year (cf. [AWS Outages](https://en.wikipedia.org/wiki/Timeline_of_Amazon_Web_Services#Amazon_Web_Services_outages), [Google Outages](https://en.wikipedia.org/wiki/Google_services_outages#:~:text=During%20eight%20episodes%2C%20one%20in,Google%20service%20in%20August%202013)). You must run your applications in multiple locations so that you can provide uninterrupted service to your users.
<!--
{{<tip>}}
To make your global applications fault-tolerant and highly available, see  [Design Patterns for HA](./design-patterns-ha)
{{</tip>}}
-->
### Data Residency for Compliance

To comply with data residency laws in each country, companies operating in that country must ensure that the data of their citizens is stored on servers located within that country (for example, the [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)). This means that companies need to design their applications to split data across geographies accordingly.

<!--
{{<tip>}}
To understand various paradigms that can help you with complying to data residency laws, see [Compliance Patterns](./global-performance)
{{</tip>}}
-->
### Moving closer to users

When designing today's applications (eg. email, e-commerce websites, or broadcasting events like the Olympics), it's essential to consider that users could be located in various geographies. For instance, if your application is hosted in data centers located in the US, users in Europe might encounter high latency when trying to access your application. To provide the best user experience, it's crucial to run your applications closer to your users.

<!--
{{<tip>}}
To enhance the performance of your global applications, see  [Performance Patterns](./global-performance)
{{</tip>}}
-->
## Application Design Patterns

Running applications in multiple data centers with data split across them is not a trivial task. But YugabyteDB can be deployed in various configurations like single-region multi-zone configuration or multi-region multi-zone with ease. You can leverage some of our battle-tested design paradigms, which offer solutions to common problems faced in these scenarios. These proven paradigms offer solutions that can significantly accelerate your application development by saving time and resources that would otherwise be spent reinventing the wheel.

Let's look at a few classes of design patterns that you can adopt with YugabyteDB.

### Fault Tolerance and High Availability

To provide uninterrupted service to your users, it's crucial to ensure that your applications can handle machine failures, network outages, and power failures. Global applications must be deployed in multiple locations with standby locations that can take over when the primary location fails. You can deploy YugabyteDB in the following configurations to ensure your applications are highly available.

#### Single cluster spread across multiple regions

You can set up your cluster across different regions/zones with multiple replicas (typically 3) such that the replicas are in different regions/zones. When a node fails in a region or an entire region/zone fails, a replica in another region/zone will be promoted to leader in seconds, without any loss of data. This is possible because of the [synchronous replication using the raft consensus protocol](../../../architecture/docdb-replication/replication).

{{<tip>}}
For more information, see  [Stretch Cluster](./design-patterns-ha#stretch-cluster)
{{</tip>}}

#### Two clusters serving data together

You can set up a separate cluster with [xCluster](../../../architecture/docdb-replication/async-replication/) replication. Replication can be configured to be either unidirectional, which is useful to create a standby cluster or bidirectional, which would enable you to write to both clusters at the same time. The key thing to remember in xCluster is that it uses asynchronous replication, which means updates will not wait for the other universe to catch up and the two clusters could be out of sync for a while.

{{<tip>}}
For more information, see  [Active-Active Multi-Master](./design-patterns-ha#active-active-multi-master)
{{</tip>}}

### Performance

#### Reduce latency with preferred leaders

By default, all leaders are distributed across multiple regions in a global application. As all writes and reads go to the leaders this leads to an increase in latency even for a simple query as the table leader and index leader could be in different regions. You can ensure all leaders are located in the same region by setting up preferred zones for leaders.

{{<tip>}}
For more details, see  [Preferred Leaders](./global-performance#reducing-latency-with-preferred-leaders)
{{</tip>}}

#### Reduce read latency with read replicas

Just like how you can set up alternate clusters and replicas for fault tolerance, you can set up a separate [read replica](../../../architecture/docdb-replication/read-replicas/) cluster to improve the read latency in a different region. Read replicas are non-voting members of the raft group and can have a different replication factor from the primary cluster.

{{<tip>}}
For more information, see  [Read Replicas](./design-patterns-ha#read-replica)
{{</tip>}}

### Compliance

To comply with data residency laws in various countries, you might have to place the data of their respective citizens in data centers in that country. YugabyteDB offers a few patterns to ensure such rules are complied with correctly.

#### Pinning tables to local geographies

Different tables can be attached to different [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) setup for different geographies. With this, you can ensure that certain tables are in specific geographies.

{{<tip>}}
For more details, see  [Geo local tables](./design-patterns-compliance#geo-local-tables)
{{</tip>}}

#### Pinning partitions to local geographies

Your table data can be [partitioned](../../../explore/ysql-language-features/advanced-features/partitions/) and attached to [tablespaces](../../../explore/ysql-language-features/going-beyond-sql/tablespaces/) setup for different geographies. With this, you can ensure that the rows belonging to different users will be located in their respective countries.

{{<tip>}}
For more details, see  [Pinning partitions](./design-patterns-compliance#geo-partitioned-tables)
{{</tip>}}

### More patterns

| Pattern | Description |
| ------- | ----------- |
| [Stretch cluster](./design-patterns-ha#stretch-cluster) | Distribute your cluster across different regions |
| [Active-Active Multi-Master](./design-patterns-ha#active-active-multi-master) | Two clusters that can handle both writes and reads |
| [Active-Active Single-Master](./design-patterns-ha#active-active-single-master) | Second cluster than can be used for deploy testing |
| [Read Replicas](./design-patterns-ha#read-replica) | Separate follower cluster for reducing read latency |
| [Geo Local Tables](./design-patterns-compliance#pinning-tables-to-local-geographies) | Place tables in different geographies |
| [Geo Partition Tables](./design-patterns-compliance#pinning-partitions-to-local-geographies) | Split your table and place specific rows in a different geography |
| [Identity Index](./global-performance#identity-indexes) | Consistent local reads in multiple regions |
| [Cluster-aware load balancing](./global-apps-smart-driver#cluster-aware-load-balancing) | Load balance your cluster with no cost |
| [Cluster-aware failover](./global-apps-smart-driver#cluster-aware-failover) | Failover to a different region automatically |

Adopting such design patterns can vastly accelerate your application development. These are proven paradigms that would save time without having to reinvent solutions.

{{<tip>}}
For more design patterns, see  [Design Patterns for global applications](./design-patterns)
{{</tip>}}

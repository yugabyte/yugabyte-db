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

### Data Residency for Compliance

To comply with data residency laws in each country, companies operating in that country must ensure that the data of their citizens is stored on servers located within that country (for example, the [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)). This means that companies need to design their applications to split data across geographies accordingly.

### Moving closer to users

When designing today's applications (eg. email, e-commerce websites, or broadcasting events like the Olympics), it's essential to consider that users could be located in various geographies. For instance, if your application is hosted in data centers located in the US, users in Europe might encounter high latency when trying to access your application. To provide the best user experience, it's crucial to run your applications closer to your users.

## The need for Application Design Patterns

Running applications in multiple data centers with data split across them is not a trivial task. But YugabyteDB can be deployed in various configurations like single-region multi-zone configuration or multi-region multi-zone with ease. You can leverage some of our battle-tested design paradigms, which offer solutions to common problems faced in these scenarios. These proven paradigms offer solutions that can significantly accelerate your application development by saving time and resources that would otherwise be spent reinventing the wheel.

Let's look at a few classes of application design patterns that you can adopt with YugabyteDB.

|                | Follow the Application | Geo-Local Data |
| -------------- | ---------------------- | -------------- |
| **Single Active**  | [Global database](./design-patterns-ha#stretch-cluster)    |      N/A |
| **Multi Active**   | [Duplicate indexes](./global-performance#identity-indexes) | [Active-active multi master](./active-active-multi-master) |
| **Partitioned Multi Active** | [Latency-optimized geo-partitioning](./latency-optimized-geo-partition) | [Locality-optimized geo-partitioning](./locality-optimized-geo-partition) |

| Data Access Architectures |
| ------------------------- |
| [Follower Reads](./follower-reads) |


### Global database - Single cluster spread across multiple regions

You can set up your cluster across different regions/zones with multiple replicas (typically 3) such that the replicas are in different regions/zones. When a node fails in a region or an entire region/zone fails, a replica in another region/zone will be promoted to leader in seconds, without any loss of data. This is possible because of the [synchronous replication using the raft consensus protocol](../../../architecture/docdb-replication/replication).

{{<tip>}}
For more information, see  [Global database](./global-database)
{{</tip>}}

### Active-active multi-master - Two clusters serving data together

Set up two separate clusters which would both handle reads and writes. Data is replicated asynchronously between the clusters.

{{<tip>}}
For more information, see  [Active-Active Multi-Master](./active-active-multi-master)
{{</tip>}}


### Active-Active Single-Master - Standby cluster

Set up a second cluster that gets populated asynchronously and can start serving data in case the primary fails. Can also be used for [blue/green](https://en.wikipedia.org/wiki/Blue-green_deployment) deploy testing.

{{<tip>}}
For more information, see  [Active-Active Single-Master](./active-active-single-master)
{{</tip>}}

### Duplicate indexes - Consistent data everywhere

Setup covering indexes with schema the same as the table in multiple regions to read immediately consistent data locally.

{{<tip>}}
For more information, see  [Duplicate indexes](./duplicate-indexes)
{{</tip>}}

### Locality-optimized geo-partitioning - For compliance

Partition your data and place them in a manner that the rows belonging to different users will be located in their respective countries.

{{<tip>}}
For more details, see  [Locality-optimized geo-partitioning](./locality-optimized-geo-partition)
{{</tip>}}

### Latency-optimized geo-partitioning - For fast local access

Partition your data and place them in a manner that the data belonging to nearby users can be accessed faster.

{{<tip>}}
For more details, see  [Latency-optimized geo-partitioning](./latency-optimized-geo-partition)
{{</tip>}}

Adopting such design patterns can vastly accelerate your application development. These are proven paradigms that would save time without having to reinvent solutions.

### Follower Reads - Fast stale reads

Read from local followers instead of going to the leaders in a different region.

{{<tip>}}
For more details, see  [Follower Reads](./follower-reads)
{{</tip>}}
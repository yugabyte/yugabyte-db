---
title: Build global applications
headerTitle: Build global applications
linkTitle: Build global applications
description: Build globally distributed applications.
headcontent: Learn how to design globally distributed applications using simple patterns
image: /images/section_icons/architecture/distributed_acid.png
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

Although public clouds have come a long way since the inception of AWS in 2006, region and zone outages are still fairly common, happening once or twice a year (cf. [AWS Outages](https://en.wikipedia.org/wiki/Timeline_of_Amazon_Web_Services#Amazon_Web_Services_outages), [Google Outages](https://en.wikipedia.org/wiki/Google_services_outages#:~:text=During%20eight%20episodes%2C%20one%20in,Google%20service%20in%20August%202013)). You must run your applications in multiple locations to provide uninterrupted service to your users.

### Data Residency for Compliance

To comply with data residency laws in each country, companies operating in that country must ensure that the data of their citizens is stored on servers located within that country (for example, the [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)). This means that companies need to design their applications to split data across geographies accordingly.

### Moving closer to users

When designing today's applications (eg. email, e-commerce websites, or broadcasting events like the Olympics), it's essential to consider that users could be located in various geographies. For instance, if your application is hosted in data centers located in the US, users in Europe might encounter high latency when trying to access your application. To provide the best user experience, it's crucial to run your applications closer to your users.

## Application design patterns

Running applications in multiple data centers with data split across them is not a trivial task. YugabyteDB can be deployed in various configurations like single-region multi-zone or multi-region multi-zone configuration with ease. You can leverage some of our battle-tested design paradigms, which offer solutions to common problems faced in these scenarios. These proven paradigms offer solutions that can significantly accelerate your application development by saving time and resources that would otherwise be spent reinventing the wheel.

We can classify these patterns based on how multiple instances of the application operate on parts of the data or whether the application follows the workload or if the application operates on local data or only reads from local followers. For example,

1. **Single Active** - The application is active in one region.
1. **Multi-Active**  - Applications run in different regions and operate on all the cluster data.
1. **Partitioned Multi-Active** - Multiple applications run in multiple regions and operate on just the local data.
1. **Follow the workload** - Applications run closer to the leaders.
1. **Geo-local dataset** - Applications read from geographically placed local data.
1. **Data Access** - Applications just read from local replicas or do strongly consistent reads.

Let's look at some of these application design patterns.
<!--
|         Pattern Type         |                         Follow the Workload                          |                              Geo-Local Data                               |
| ---------------------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| **Single Active**            | [Global database](./global-database)                                    | N/A                                                                       |
| **Multi Active**             | [Duplicate indexes](./duplicate-indexes)                                | [Active-active multi master](./active-active-multi-master)                |
| **Partitioned Multi Active** | [Latency-optimized geo-partitioning](./latency-optimized-geo-partition) | [Locality-optimized geo-partitioning](./locality-optimized-geo-partition) |
| **Access only**              | [Follower Reads](./follower-reads), [Read Replicas](./read-replicas)    | N/A                                                                       |

Let's look at a quick overview of each of these patterns.
-->
{{<table>}}

| Pattern | Category | Description |
| ------- | -------- | ----------- |
| [Global database](./global-database) | **Single Active**, _Follow the Workload_ |
{{<header Level="6">}} Single cluster spread across multiple regions {{</header>}}
A cluster with replicas spread across multiple regions/zones. On failure, a replica in another region/zone will be promoted to leader in seconds, without any loss of data.|

|[Active&#8209;Active Multi&#8209;Master](./active-active-multi-master)| **Multi Active**, _Geo-Local Data_ |
{{<header Level="6">}} Two clusters serving data together {{</header>}}
Set up two separate clusters which would both handle reads and writes. Data is replicated asynchronously between the clusters|

|[Active&#8209;Active Single&#8209;Master](./active-active-single-master)| **Single Active**, Follow the Workload |
{{<header Level="6">}} Standby cluster {{</header>}}
Set up a second cluster that gets populated asynchronously and can start serving data in case the primary fails. Can also be used for [blue/green](https://en.wikipedia.org/wiki/Blue-green_deployment) deployment testing|

|[Duplicate indexes](./duplicate-indexes)| **Multi Active**, _Follow the Workload_ |
{{<header Level="6">}} Consistent data everywhere {{</header>}}
Set up covering indexes with schema the same as the table in multiple regions to read immediately consistent data locally|

|[Locality&#8209;optimized geo&#8209;partitioning](./locality-optimized-geo-partition)| **Multi Active**, _Geo-Local Data_ |
{{<header Level="6">}} Local law compliance {{</header>}}
Partition your data and place them in a manner that the rows belonging to different users will be located in their respective countries|

|[Latency&#8209;optimized geo&#8209;partitioning](./latency-optimized-geo-partition)| Single Active, Follow the Workload |
{{<header Level="6">}} Fast local access {{</header>}}
Partition your data and place them in a manner that the data belonging to nearby users can be accessed faster|

|[Follower Reads](./follower-reads) | Single Active, Follow the Workload |
{{<header Level="6">}} Fast, stale reads {{</header>}}
Read from local followers instead of going to the leaders in a different region|

|[Read Replicas](./read-replicas) | Single Active, Follow the Workload |
{{<header Level="6">}} Fast, stale reads {{</header>}}
Set up a separate cluster of just followers to perform local reads instead of going to the leaders in a different region|

{{</table>}}

Adopting such design patterns can vastly accelerate your application development. These are proven paradigms that would save time without having to reinvent solutions.

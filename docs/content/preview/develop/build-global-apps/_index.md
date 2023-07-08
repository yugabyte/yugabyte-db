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
showRightNav: true
---

{{<srcdiagram href="https://docs.google.com/presentation/d/1lEajQyVZLhmHRKmBxunf1LucWkQkrJ3rIthoHxZvyQc/edit#slide=id.g22bc5dd47b0_0_18">}}

In today's fast-paced world, the internet and cloud technology have revolutionized the way people interact and operate. Cloud introduces multiple regions leading to data being distributed and replicated over various geographies. A new class of applications must be developed to access and maintain globally distributed data. Let's understand the reasons why today's applications have to be global and look at some patterns that YugabyteDB offers to design your global applications with ease.

## The need for global applications

The reasons for making your applications global are the same as those for adopting a distributed database:

- **Business continuity and disaster recovery** Although public clouds have come a long way since the inception of AWS in 2006, region and zone outages are still fairly common, happening once or twice a year (see, for example, [AWS outages](https://en.wikipedia.org/wiki/Timeline_of_Amazon_Web_Services#Amazon_Web_Services_outages) and [Google outages](https://en.wikipedia.org/wiki/Google_services_outages#:~:text=During%20eight%20episodes%2C%20one%20in,Google%20service%20in%20August%202013)). To provide uninterrupted service to your users, you need to run your applications in multiple locations.

- **Data residency for compliance** To comply with data residency laws (for example, the [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)) in each country, you need to ensure that the data of citizens is stored on servers located in their country. This means that you need to design your applications to split data across geographies accordingly.

- **Moving data closer to users** When designing an application with global reach (for example, email, e-commerce, or broadcasting events like the Olympics), you need to take into account that users could be located in various geographies. If your application is hosted in data centers located in the US, users in Europe might encounter high latency when trying to access your application. To provide the best user experience, you need to run your applications closer to your users.

## Global application design patterns

To simplify the task of designing a global application, leverage the following tested design patterns. These patterns offer solutions to common problems faced in developing global applications, and can accelerate your application development.

### Application Architecture

- **Single Active** - Only one application instance in a region (or fault domain) is active. The data must be placed close to that application.
- **Multi-Active** - Applications run in different regions and operate on all the cluster data.
- **Partitioned Multi-Active** - Multiple applications run in multiple regions and operate on just the local data.

### Availability Architecture

- **Follow the application** - Applications run closer to the leaders. On failure, the application moves to a different region.
- **Geo-local dataset** - Applications read from geographically placed local data. On failure, the application does not move.

### Data Access Architectures

- **Consistent reads** - Read from the source of truth, irrespective of latency or location.
- **Follower reads** - Allow stale reads to achieve lower latency reads.
- **Bounded staleness** - Allow stale reads but with bounds on how stale data is.

## Picking the right design pattern

The pattern you choose depends on the specific requirements of your deployment and use case, such as multiple instances of the application operating on all data or specific parts of it, workload alignment with the application, or the application solely reading from local followers.

|         Pattern Type         |                         Follow the Application                          |                              Geo-Local Data                               |
| ---------------------------- | ----------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| **Single Active**            | [Global database](./global-database)                                    | N/A                                                                       |
| **Multi Active**             | [Duplicate indexes](./duplicate-indexes)                                | [Active-active multi master](./active-active-multi-master)                |
| **Partitioned Multi Active** | [Latency-optimized geo-partitioning](./latency-optimized-geo-partition) | [Locality-optimized geo-partitioning](./locality-optimized-geo-partition) |
| **Data Access only**         | [Follower Reads](./follower-reads), [Read Replicas](./read-replicas)    | N/A                                                                       |

## Design patterns explained

We will use the following legend to represent tablet leaders/followers, cloud regions/zones and applications to explain the design patterns in detail.

![Global Database - Legend](/images/develop/global-apps/global-database-legend.png)

{{<table>}}

| Pattern Name | Description |
| ------- | ----------- |
| [Global database](./global-database) |
{{<header Level="6">}} Single database spread across multiple regions {{</header>}}
A database spread across multiple(3 or more) regions/zones. On failure, a replica in another region/zone will be promoted to leader in seconds, without any loss of data.
Applications read from source of truth, possibly with higher latencies|

|[Active&#8209;Active Single&#8209;Master](./active-active-single-master)|
{{<header Level="6">}} Secondary database that can serve reads {{</header>}}
Set up a second cluster that gets populated asynchronously and can start serving data in case the primary fails. Can also be used for [blue/green](https://en.wikipedia.org/wiki/Blue-green_deployment) deployment testing|

|[Active&#8209;Active Multi&#8209;Master](./active-active-multi-master)|
{{<header Level="6">}} Two clusters serving data together {{</header>}}
Two regions or more, manual failover, a few seconds of data loss (non-zero RPO), low read/write latencies, some caveats on transactional guarantees|

|[Duplicate indexes](./duplicate-indexes)|
{{<header Level="6">}} Consistent data everywhere {{</header>}}
Set up covering indexes with schema the same as the table in multiple regions to read immediately consistent data locally|

|[Locality&#8209;optimized geo&#8209;partitioning](./locality-optimized-geo-partition)|
{{<header Level="6">}} Local law compliance {{</header>}}
Partition your data and place them in a manner that the rows belonging to different users will be located in their respective countries|

|[Latency&#8209;optimized geo&#8209;partitioning](./latency-optimized-geo-partition)|
{{<header Level="6">}} Fast local access {{</header>}}
Partition your data and place them in a manner that the data belonging to nearby users can be accessed faster|

|[Follower Reads](./follower-reads) |
{{<header Level="6">}} Fast, stale reads {{</header>}}
Read from local followers instead of going to the leaders in a different region|

|[Read Replicas](./read-replicas) |
{{<header Level="6">}} Fast reads from a read-only cluster {{</header>}}
Set up a separate cluster of just followers to perform local reads instead of going to the leaders in a different region|

{{</table>}}

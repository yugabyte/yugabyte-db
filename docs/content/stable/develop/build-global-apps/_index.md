---
title: Build global applications
headerTitle: Build global applications
linkTitle: Build global applications
description: Build globally distributed applications.
headcontent: Learn how to design globally distributed applications using simple patterns
image: /images/section_icons/architecture/distributed_acid.png
menu:
  stable:
    identifier: build-global-apps
    parent: develop
    weight: 201
type: indexpage
showRightNav: true
---

{{<srcdiagram href="https://docs.google.com/presentation/d/1lEajQyVZLhmHRKmBxunf1LucWkQkrJ3rIthoHxZvyQc/edit#slide=id.g22bc5dd47b0_0_18">}}

Internet and cloud technologies have revolutionized the way people interact and operate. Cloud introduces the possibility of distributing and replicating data across multiple geographies. Accessing and maintaining that globally distributed data demands a new class of global application.

## The need for global applications

The reasons for making your applications global are the same as those for adopting a distributed database:

- **Business continuity and disaster recovery**. Although public clouds have come a long way since the inception of AWS in 2006, region and zone outages are still fairly common, happening once or twice a year (see, for example, [AWS outages](https://en.wikipedia.org/wiki/Timeline_of_Amazon_Web_Services#Amazon_Web_Services_outages) and [Google outages](https://en.wikipedia.org/wiki/Google_services_outages#:~:text=During%20eight%20episodes%2C%20one%20in,Google%20service%20in%20August%202013)). To provide uninterrupted service to your users, you need to run your applications in multiple locations.

- **Data residency for compliance**. To comply with data residency laws (for example, the [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)), you need to ensure that the data of citizens is stored on servers located in their country. This means that you need to design your applications to split data across geographies accordingly.

- **Moving data closer to users**. When designing an application with global reach (for example, email, e-commerce, or broadcasting events like the Olympics), you need to take into account where your users are located. If your application is hosted in data centers located in the US, users in Europe might encounter high latency when trying to access your application. To provide the best user experience, you need to run your applications closer to your users.

## Application design patterns

Running applications in multiple data centers with data split across them is not a trivial task. When designing global applications, you need to answer questions such as:

- How will multiple instances of the application run in different fault domains (regions/data centers)?
- Will the application instances be identical or different?
- How will these applications be deployed across geo-locations?
- Will each instance operate on the entire dataset or just a subset of the data?
- Will conflicting application updates be allowed? If so, how are these handled?
- How will the application evolve?
- How will the application behave on a fault domain failure?

To help you answer these questions, use the following architectural concepts to choose a suitable design pattern for your application.

### Application architecture

Depending on where the application instances run and which ones are active, choose from the following application architectures:

- **Single Active** - Only one application instance in a region (or fault domain) is active. The data must be placed close to that application. On failure, the application moves to a different region.
- **Multi-Active** - Applications run in different regions and operate on the entire data set.
- **Read-Only Multi-Active** - Only one application instance is active, while the others can serve stale reads.
- **Partitioned Multi-Active** - Multiple applications run in multiple regions and operate on just a subset of data.

### Availability architecture

Depending on whether the application instances operate on the entire dataset or just a subset, and how the application moves on a fault domain failure, choose from the following availability architectures:

- **Follow the application** - Only one application instance is active, while the others (one or more) can serve stale reads.
- **Geo-local dataset** - Applications act on geographically placed local data. On failure, the application does not move.

### Data access architecture

Depending on whether the application should read the latest data or stale data, choose from the following data access architectures:

- **Consistent reads** - Read from the source of truth, irrespective of latency or location.
- **Follower reads** - Stale reads to achieve lower latency reads.
- **Read replicas** - Allow stale reads but with bounds on how stale data is.

## Pick the right pattern

Use the following matrix to choose a [design pattern](#design-patterns), based on the architectures described in the preceding section.

|         Pattern Type         |                                       Follow the Application                                       |                              Geo-Local Dataset                               |
| ---------------------------- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| **Single Active**            | [Global database](./global-database)<br>[Active-active single-master](./active-active-single-master) | N/A                                                                       |
| **Multi Active**             | [Global database](./global-database)<br>[Duplicate indexes](./duplicate-indexes)                   | [Active-active multi-master](./active-active-multi-master)                |
| **Partitioned Multi Active** | [Latency-optimized geo-partitioning](./latency-optimized-geo-partition)                            | [Locality-optimized geo-partitioning](./locality-optimized-geo-partition) |
| **Data Access Architecture** | [Consistent Reads](./global-database)<br>[Follower Reads](./follower-reads)<br>[Read Replicas](./read-replicas)         |                                                                       |

## Design patterns

The following table summarizes the design patterns that you can use for your applications. Use these proven patterns to address common problems and accelerate your application development.

{{<table>}}

| Pattern Name | Description |
| ------- | ----------- |
| [Global database](./global-database) |
{{<header Level="6">}} Single database spread across multiple regions {{</header>}}
A database spread across multiple (3 or more) regions or zones. On failure, a replica in another region/zone will be promoted to leader in seconds, without any loss of data. Applications read from source of truth, possibly with higher latencies.|

|[Duplicate indexes](./duplicate-indexes)|
{{<header Level="6">}} Consistent data everywhere {{</header>}}
Set up covering indexes with schema the same as the table in multiple regions to read immediately consistent data locally.|

|[Active&#8209;active single&#8209;master](./active-active-single-master)|
{{<header Level="6">}} Secondary database that can serve reads {{</header>}}
Set up a second cluster that gets populated asynchronously and can start serving data in case the primary fails. Can also be used for [blue-green](https://en.wikipedia.org/wiki/Blue-green_deployment) deployment testing.|

|[Active&#8209;active multi&#8209;master](./active-active-multi-master)|
{{<header Level="6">}} Two clusters serving data together {{</header>}}
Two regions or more, manual failover, a few seconds of data loss (non-zero RPO), low read/write latencies, some caveats on transactional guarantees.|

|[Latency&#8209;optimized geo&#8209;partitioning](./latency-optimized-geo-partition)|
{{<header Level="6">}} Fast local access {{</header>}}
Partition your data and place it such that the data belonging to nearby users can be accessed faster.|

|[Locality&#8209;optimized geo&#8209;partitioning](./locality-optimized-geo-partition)|
{{<header Level="6">}} Local law compliance {{</header>}}
Partition your data and place it such that the rows belonging to different users are located in their respective countries.|

|[Follower Reads](./follower-reads) |
{{<header Level="6">}} Fast, stale reads {{</header>}}
Read from local followers instead of going to the leaders in a different region.|

|[Read Replicas](./read-replicas) |
{{<header Level="6">}} Fast reads from a read-only cluster {{</header>}}
Set up a separate cluster of just followers to perform local reads instead of going to the leaders in a different region.|

{{</table>}}

## Legend

All the illustrations in this section use the following legend to represent tablet leaders and followers, cloud regions and zones, and applications.

![Global Database - Legend](/images/develop/global-apps/global-database-legend.png)

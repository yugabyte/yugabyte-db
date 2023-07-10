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

### Business Continuity and Disaster Recovery

Although public clouds have come a long way since the inception of AWS in 2006, region and zone outages are still fairly common, happening once or twice a year (cf. [AWS Outages](https://en.wikipedia.org/wiki/Timeline_of_Amazon_Web_Services#Amazon_Web_Services_outages), [Google Outages](https://en.wikipedia.org/wiki/Google_services_outages#:~:text=During%20eight%20episodes%2C%20one%20in,Google%20service%20in%20August%202013)). You must run your applications in multiple locations to provide uninterrupted service to your users.

### Data Residency for Compliance

To comply with data residency laws in each country, companies operating in that country must ensure that the data of their citizens is stored on servers located within that country (for example, the [GDPR](https://en.wikipedia.org/wiki/General_Data_Protection_Regulation)). This means that companies need to design their applications to split data across geographies accordingly.

### Moving closer to users

When designing today's applications (eg. email, e-commerce websites, or broadcasting events like the Olympics), it's essential to consider that users could be located in various geographies. For instance, if your application is hosted in data centers located in the US, users in Europe might encounter high latency when trying to access your application. To provide the best user experience, it's crucial to run your applications closer to your users.

## Application design patterns

Running applications in multiple data centers with data split across them is not a trivial task. When designing global applications, a lot of questions need to be answered first. Like,

- How multiple instances of the application would run in different fault domains (regions/data centers)?
- Will the application instances be identical or different?
- How will these applications be deployed across geo-locations?
- Would each instance operate on the entire dataset or just a subset of the data?
- Would conflicting app updates be allowed? If so, how are these handled?
- How will the application evolve?
- How will the application behave on a fault domain failure?

To guide you through the path to answer such questions, we have defined several architectural concepts with which you would be able to choose a good design pattern for your application.

### Application Architecture

Depending on where the application instances run and which ones are active, we define a few types of application architectures that you can choose from.

- **Single Active** - Only one application instance in a region (or fault domain) is active. The data must be placed close to that application. On failure, the application moves to a different region.
- **Multi-Active** - Applications run in different regions and operate on the entire data set.
- **Read-Only Multi-Active** - Only one application instance is active, while the others can serve stale reads
- **Partitioned Multi-Active** - Multiple applications run in multiple regions and operate on just a subset of data.

### Availability Architecture

Depending on whether the application instances operate on the entire dataset or just a subset, and how the application moves on a fault domain failure, we define a few types of availability architectures that you can choose from.

- **Follow the application** - Only one application instance is active, while the others (one or more) can serve stale reads
- **Geo-local dataset** - Applications act on geographically placed local data. On failure, the application does not move.

### Data Access Architectures

Depending on whether the application should read the latest data or stale data, we define a few types of data access architectures that you can choose from.

- **Consistent reads** - Read from the source of truth, irrespective of latency or location
- **Follower reads** - Stale reads  to achieve lower latency reads
- **Bounded staleness** - Allow stale reads but with bounds on how stale data is

## Picking the right design pattern

Now, we fit various design patterns into the different architectures (Application/Availability/Data Access) we have defined above.

|         Pattern Type         |                                       Follow the Application                                       |                              Geo-Local Data                               |
| ---------------------------- | -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| **Single Active**            | [Global database](./global-database)<br>[Active-active multi-master](./active-active-multi-master) | N/A                                                                       |
| **Multi Active**             | [Global database](./global-database)<br>[Duplicate indexes](./duplicate-indexes)                   | [Active-active multi-master](./active-active-multi-master)                |
| **Partitioned Multi Active** | [Latency-optimized geo-partitioning](./latency-optimized-geo-partition)                            | [Locality-optimized geo-partitioning](./locality-optimized-geo-partition) |
| **DATA ACCESS ARCHITECTURE** | [Consistent Reads](./global-database)<br>[Follower Reads](./follower-reads)<br>[Read Replicas](./read-replicas)         |                                                                       |

## Design patterns explained

{{<note>}}
We will use the following legend to represent tablet leaders/followers, cloud regions/zones and applications to explain the design patterns in detail.
{{</note>}}

![Global Database - Legend](/images/develop/global-apps/global-database-legend.png)

{{<table>}}

| Pattern Name | Description |
| ------- | ----------- |
| [Global database](./global-database) |
{{<header Level="6">}} Single database spread across multiple regions {{</header>}}
A database spread across multiple(3 or more) regions/zones. On failure, a replica in another region/zone will be promoted to leader in seconds, without any loss of data.
Applications read from source of truth, possibly with higher latencies|

|[Duplicate indexes](./duplicate-indexes)|
{{<header Level="6">}} Consistent data everywhere {{</header>}}
Set up covering indexes with schema the same as the table in multiple regions to read immediately consistent data locally|

|[Active&#8209;Active Single&#8209;Master](./active-active-single-master)|
{{<header Level="6">}} Secondary database that can serve reads {{</header>}}
Set up a second cluster that gets populated asynchronously and can start serving data in case the primary fails. Can also be used for [blue/green](https://en.wikipedia.org/wiki/Blue-green_deployment) deployment testing|

|[Active&#8209;Active Multi&#8209;Master](./active-active-multi-master)|
{{<header Level="6">}} Two clusters serving data together {{</header>}}
Two regions or more, manual failover, a few seconds of data loss (non-zero RPO), low read/write latencies, some caveats on transactional guarantees|

|[Latency&#8209;optimized geo&#8209;partitioning](./latency-optimized-geo-partition)|
{{<header Level="6">}} Fast local access {{</header>}}
Partition your data and place them in a manner that the data belonging to nearby users can be accessed faster|

|[Locality&#8209;optimized geo&#8209;partitioning](./locality-optimized-geo-partition)|
{{<header Level="6">}} Local law compliance {{</header>}}
Partition your data and place them in a manner that the rows belonging to different users will be located in their respective countries|

|[Follower Reads](./follower-reads) |
{{<header Level="6">}} Fast, stale reads {{</header>}}
Read from local followers instead of going to the leaders in a different region|

|[Read Replicas](./read-replicas) |
{{<header Level="6">}} Fast reads from a read-only cluster {{</header>}}
Set up a separate cluster of just followers to perform local reads instead of going to the leaders in a different region|

{{</table>}}

You can leverage some of our battle-tested design paradigms, which offer solutions to common problems faced in many scenarios. Adopting such proven paradigms offer solutions that can significantly accelerate your application development by saving time and resources that would otherwise be spent reinventing the wheel.

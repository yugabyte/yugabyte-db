# Goals

YugaByteDB is a unified database purpose-built to power modern, distributed cloud services. 

A distributed cloud services need a **distributed database** if one of the following is true:
- need scale out in order to handle high QPS (queries per second)
- the data set is large enough that scaling up one machine is not viable
- the data needs to be geo-replicated

YugaByte was built with the following design goals:
- **highly scalable**
- **highly resilient**
- **very performant**
- **cloud-native**

In order to support the needs of a wide array of distributed cloud services without imposing the necessity to learn a new API, YugaByte can be accessed using one or more of the following APIs on top of a common data fabric underneath:
- **Redis** APIs
- **Cassandra** Query Language (CQL)
- **SQL** (PostgreSQL - in progress)


# Features needed to build distributed cloud services

## Operational features needed
- **High scalability** Add or remove nodes at any time to increase the aggregate CPU, RAM or storage capacity of the serving cluster
- **Fault tolerance** Should be able to tolerate failures with minimal downtime and without external intervention
- **Highly resilient** When long-lasting errors happen, the database should re-replicate the data and satisfy the user intent
- **Intent based deployment** Changes to configuration may happen frequently, these changes should be auto-enforced without any application or service outage

## Database features needed

![Feature-oriented view of YugaByte design](https://github.com/YugaByte/yugabyte-db/blob/master/docs/images/yugabytedb-design-philosophy.png)

The figure above summarizes the core database features needed to build modern distributed cloud service (and those that are not).

The necessary features needed to build distributed cloud services are:
- **Strong consistency** This refers to *single row ACID* transactions, where consistency is enforced for a single key.
- **Consistent secondary Indexes** Maintain secondary indexes on some pre-defined set of columns or attributes, and query them efficiently.
- **Multi-row ACID transactions** Allow operations that insert, update or delete multiple rows with transactional semantics
- **Multi-DC deployments** Maintain the configured number of copies of the data across multiple geographies, and keep the data consistent and resilient even if failure are encountered
- **JSON / document support in DB** A lot applications need to store objects which have a variable set of attributes, and allow operations such as secondary indexes and filtering on these
- **Tunable read consistency** Allow timeline-consistent reads from followers or read-replicas, especially ability to read from a nearby datacenter
- **Expiring older data with TTLs** Allow data to be expired after a certain time interval, for example deleting the raw timeseries data points 1 year after they are written
- **High write throughput** Ability to perform efficient batch operations, especially writes
- **Run Apache Spark for AI/ML** Should be able to work with a real-time analytics system such as Spark Streaming, which is very useful services that depend on machine learning to expose interesting information to end-users

## Database features NOT needed

1. In a scale out architecture, nodes are typically added to improve performance. By choosing a service architecture which causes the performance of the system as a whole to degrade with the addition of nodes, we have defeated the purpose of scale-out distributed cloud service. The following features cause performance degradation on scaling out:
    - Foreign key constraints
    - Distributed joins

2. Eventual consistency does not work when building distributed, user-facing services because of the need for consistent responses. Hence the following feature is removed from the list:
    - Eventual consistency on writes

Note that master-slave async replication (which has timeline consistency) as well as reading from followers/read-replicas are not excluded, and these do not fall under eventual consistency.


# Architecture

## Overview

In terms of the traditional CAP theorem, YugaByteDB is a CP database (consistent and partition tolerant), but achieves very high availability. The architectural design of YugaByte is similar to Google Cloud Spanner, which is also a CP system. YugaByteDB has no single points of failure.

A YugaByte cluster, also referred to as a universe, is a group of nodes (VMs, physical machines or containers) that collectively function as a highly available and resilient database. A YugaByte universe can be deployed in a variety of configurations depending on business requirements, and latency considerations. Some examples:
- Single availability zone (AZ/rack/failure domain)
- Multiple AZs in a region
- Multiple regions (with synchronous and asynchronous replication choices)


Every YugaByteDB node is comprised of 2 layers:

- **YBase** This is the core data fabric. It is a highly available, distributed system with strong write consistency, tunable read consistency, and an advanced log-structured row/document-oriented storage. It includes several optimizations for handling ever-growing datasets efficiently. 

- **YQL** is the upper/edge layer that has the API specific aspects - for example, the server-side implementation of Apache Cassandra Query Language and Redis protocols, and the corresponding query/command compilation and run-time (data type representations, built-in operations, etc.).


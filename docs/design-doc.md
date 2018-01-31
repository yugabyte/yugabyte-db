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

![Feature-oriented view of YugaByte design](https://github.com/YugaByte/yugabyte-db/blob/master/docs/images/yugabytedb-design-philosophy.png)

The figure above summarizes the database-level features needed to build modern distributed cloud service (and those that are not).

## Why are some features removed?

1. In a scale out architecture, nodes are typically added to improve performance. By choosing a service architecture which causes the performance of the system as a whole to degrade with the addition of nodes, we have defeated the purpose of scale-out distributed cloud service. The following features cause performance degradation on scaling out:
    - Foreign key constraints
    - Distributed joins

2. Eventual consistency does not work when building distributed, user-facing services because of the need for consistent responses. Hence the following feature is removed from the list:
    - Eventual consistency on writes

Note that master-slave async replication (which has timeline consistency) as well as reading from followers/read-replicas are not excluded, and these do not fall under eventual consistency.


# Architecture


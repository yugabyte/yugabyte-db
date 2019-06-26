## Change Data Capture in YugaByte DB

**Change data capture** (or **CDC** for short) enables capturing changes performed to the data stored in YugaByte DB. This document provides an overview of the approach YugaByte DB uses for providing change capture stream on tables that can be consumed by third party applications. This featureis useful in a number of scenarios such as:

### Microservice-oriented architectures

There are some microservices that require a stream of changes to the data. For example, a search system powered by a service such as Elasticsearch may be used in conjunction with the database stores the transactions. The search system requires a stream of changes made to the data in YugaByte DB. 

### Asynchronous replication to remote systems

Remote systems such as caches and analytics pipelines may subscribe to the stream of changes, transform them and consume these changes.

### Two data center deployments

Two datacenter deployments in YugaByte DB leverage change data capture at the core.

> Note that in this design, the terms "data center", "cluster" and "universe" will be used interchangeably. We assume here that each YB universe is deployed in a single data-center.


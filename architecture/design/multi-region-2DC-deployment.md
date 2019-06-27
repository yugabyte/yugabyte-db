# Two Data Center Deployment Support with YugaByte DB

This document outlines the 2-datacenter deployments that YugaByte DB is being enhanced to support, as well as the architecture that enables these. This feature is built on top of [Change Data Capture (CDC)](https://github.com/YugaByte/yugabyte-db/blob/master/architecture/design/docdb-change-data-capture.md) support in DocDB, and that design may be of interest.

> Note that in this design, the terms "cluster" and "universe" will be used interchangeably. While not a requirement in the final design, we assume here that each YugaByte DB universe is deployed in a single data-center for simplicity purposes.

## Features

This feature will support the following:

* This applies to **all** the APIs at the query layer - YSQL, YCQL and YEDIS.

* This design is general enough to support replication of single-key updates as well as multi-key/distributed transactions

* In the initial version, we assume that schema changes are run on both the universes independently. This will eventually be automated to the extent possible. Note that some types of schema changes are unsafe for some types of operations - these will have to be listed out and made safe by erroring out or through some other such mechanism.

* Multiple slave clusters can be supported in this design 

## Supported Deployment Scenarios

The following 2-DC scenarios will be supported:

### Master-Slave with *asynchronous replication*

The replication could be unidirectional from the source cluster to one or more sink clusters. The source cluster is called the **master cluster** and the remote sink clusters are called **slave clusters**. The slave clusters, typically located in data centers or regions that are different from the source cluster, are *passive* because they do not not take writes from the higher layer services. Such deployments are typically used for serving low latency reads from the slave clusters as well as for disaster recovery purposes. 

### Master-Master with *last writer wins (LWW)* semantics

The replication of data can be bi-directional between two clusters. In this case, both clusters can perform reads and writes. Writes to any cluster is asynchronously replicated to the other cluster with a timestamp for the update. If the same key is updated in both clusters at a similar time window, this will result in the write with the larger timestamp becoming the latest write. Thus, in this case, the clusters are all *active* and this deployment mode is called **multi-master deployments** or **active-active deployments**).



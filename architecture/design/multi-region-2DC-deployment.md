# Two Data Center Deployment Support with YugaByte DB

This document outlines the 2-datacenter deployments that YugaByte DB is being enhanced to support, as well as the architecture that enables these.

## Functionality

Note the following functional aspects of this feature:

* This applies to **all** the APIs at the query layer - YSQL, YCQL and YEDIS.

* The implementation will follow a phased approach:
    * The first version will support single-key updates
    * Next, support for multi-key/distributed transactions will be layered on top
    * Finally, support for DDL operations getting replicated will be added

* This feature is built on top of [Change Data Capture (CDC)](https://github.com/YugaByte/yugabyte-db/blob/master/architecture/design/docdb-change-data-capture.md) support in DocDB, and that design may be of interest.

## Supported Deployment Scenarios

The following 2-DC scenarios will be supported:

### Master-Slave with *asynchronous replication*

The replication could be unidirectional from the source cluster to one or more sink clusters. The source cluster is called the **master cluster** and the remote sink clusters are called **slave clusters**. The slave clusters, typically located in data centers or regions that are different from the source cluster, are *passive* because they do not not take writes from the higher layer services. Such deployments are typically used for serving low latency reads from the slave clusters as well as for disaster recovery purposes. 

### Master-Master with *last writer wins (LWW)* semantics

The replication of data can be bi-directional between two clusters. In this case, both clusters can perform reads and writes. Writes to any cluster is asynchronously replicated to the other cluster with a timestamp for the update. If the same key is updated in both clusters at a similar time window, this will result in the write with the larger timestamp becoming the latest write. Thus, in this case, the clusters are all *active* and this deployment mode is called **multi-master deployments** or **active-active deployments**).



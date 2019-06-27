# Two Data Center Deployment Support with YugaByte DB

This document outlines the 2-datacenter deployments that YugaByte DB is being enhanced to support, as well as the architecture that enables these. This feature is built on top of [Change Data Capture (CDC)](https://github.com/YugaByte/yugabyte-db/blob/master/architecture/design/docdb-change-data-capture.md) support in DocDB, and that design may be of interest.

> **Note:** In this design document, the terms "cluster" and "universe" will be used interchangeably. While not a requirement in the final design, we assume here that each YugaByte DB universe is deployed in a single data-center for simplicity purposes.

## Features

This feature will support the following:

* Replication across datacenters is done at the DocDB level. This means that replication will work across **all** the APIs - YSQL, YCQL and YEDIS.

* The design is general enough to support replication of single-key updates as well as multi-key/distributed transactions.

* In the initial version, we assume that schema changes are run on both the universes independently. This will eventually be automated to the extent possible. Note that some combinations of schema changes and update operations are inherently unsafe. Identifying all of these cases and making the database safe in all scenarios (by throwing a user facing error out or through some other such mechanism) will be a follow on task to harden this feature.

* Note that it will be possible to perform replication to multiple target slave clusters. 

## Supported Deployment Scenarios

The following 2-DC scenarios will be supported:

### Master-Slave with *asynchronous replication*

The replication could be unidirectional from the source cluster to one or more sink clusters. The source cluster is called the **master cluster** and the remote sink clusters are called **slave clusters**. The slave clusters, typically located in data centers or regions that are different from the source cluster, are *passive* because they do not not take writes from the higher layer services. Such deployments are typically used for serving low latency reads from the slave clusters as well as for disaster recovery purposes.

The master-slave deployment architecture is shown in the diagram below:

![2DC master-slave deployment](https://github.com/YugaByte/yugabyte-db/raw/master/architecture/design/images/2DC-master-slave-deployment.png)

### Multi-Master with *last writer wins (LWW)* semantics

The replication of data can be bi-directional between two clusters. In this case, both clusters can perform reads and writes. Writes to any cluster is asynchronously replicated to the other cluster with a timestamp for the update. If the same key is updated in both clusters at a similar time window, this will result in the write with the larger timestamp becoming the latest write. Thus, in this case, the clusters are all *active* and this deployment mode is called **multi-master deployments** or **active-active deployments**).

> **NOTE:** The multi-master deployment is built internally using two master-slave unidirectional replication streams as a building block. Special care is taken to ensure that the timestamps are assigned to ensure last writer wins semantics and the data arriving from the replication stream is not re-replicated. Since master-slave is the core building block, we will focus the rest of this design document on that.


The architecture diagram is shown below:

![2DC multi-master deployment](https://github.com/YugaByte/yugabyte-db/raw/master/architecture/design/images/2DC-multi-master-deployment.png)



## Setting up 2DC Master-Slave

> **NOTE:** The commands shown in this document are more to give an idea and are not the final commands. Please refer to the documentation of this feature for the final commands.

To setup data center replication, a command similar to the following would have to be run:

```
yugabyte-db enable-universe-replication                 \
            REPLICATE_FROM <master_cluster_addresses>   \
            ROLE <role>                                 \
            [PASSWORD <password>]                       \
            --master_addresses <slave_cluster_addresses>
```

The parameters are described below:

| Parameter                      | Description |
| ------------------------------ | ------------- |
| `enable-universe-replication`  | Enables data center replication and initializes the replication worker threads on the master cluster nodes.  |
| `REPLICATE_FROM <master_cluster_addresses>`  | The list of YB-Master IP addresses for the source master universe whose data needs to be replicated to a target cluster.  |
| `ROLE <role>`  | The user/group credentials that will be used to authenticate with source master universe.  |
| `PASSWORD <password>`  | Password for the role that will be used to authenticate with source master universe. This can be provided via command-line or by setting an environment variable `$YB_REPLICATION_PASSWORD`.  |





# Two Data Center Deployment Support with YugaByte DB

This document outlines the 2-datacenter deployments that YugaByte DB is being enhanced to support, as well as the architecture that enables these. Note that these 2DC features build on top of [Change Data Capture (CDC)]() support in DocDB, and that design may be of interest.

The following 2-DC scenarios will be supported:
* Master-Slave with *asynchronous replication*
* Master-Master with *last writer wins (LWW)* semantics

The replication could be unidirectional from the source cluster to one or more sink clusters. The source cluster is called the *master cluster* and the remote sink clusters are called *slave clusters*. The slave clusters, typically located in a different data center or region, are passive because they do not not take writes from the end users. Such deployments are used for disaster recovery purposes. 

The remote cluster could be an *active cluster* in another data center  or region taking writes(a.k.a active-active data centers) widely used in modern applications.

# Setting up a CDC

*Coming soon*

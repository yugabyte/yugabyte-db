---
title: Prepare nodes for on-premises deployment
headerTitle: Prepare your network
linkTitle: Prepare your network
description: Prepare YugabyteDB nodes for on-premises deployments.
headContent: Make sure nodes can communicate
menu:
  preview_yugabyte-platform:
    identifier: prepare-on-prem-nodes
    parent: install-yugabyte-platform
    weight: 79
type: docs
---

YugabyteDB Anywhere needs to be able to access nodes that will be used to create universes, and the nodes that make up universes need to be accessible to each other and to applications.

## Prepare ports

The following ports must be opened for intra-cluster communication (they do not need to be exposed to your application, only to other nodes in the cluster and the YugabyteDB Anywhere node):

* 7100 - YB-Master RPC
* 9100 - YB-TServer RPC
* 18018 - YB Controller

The following ports must be exposed for intra-cluster communication. You should expose these ports to administrators or users monitoring the system, as these ports provide diagnostic troubleshooting and metrics:

* 9300 - Prometheus metrics
* 7000 - YB-Master HTTP endpoint
* 9000 - YB-TServer HTTP endpoint
* 11000 - YEDIS API
* 12000 - YCQL API
* 13000 - YSQL API
* 54422 - Custom SSH

The following ports must be exposed for intra-node communication and be available to your application or any user attempting to connect to the YugabyteDB universes:

* 5433 - YSQL server
* 9042 - YCQL server
* 6379 - YEDIS server

For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports).

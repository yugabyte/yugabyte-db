---
title: Cluster aware client drivers
headerTitle: Cluster-aware client drivers
linkTitle: Cluster-aware drivers
headcontent:
menu:
  stable:
    identifier: cluster-aware-drivers
    parent: going-beyond-sql
    weight: 450
rightNav:
  hideH3: true
type: docs
---

YugabyteDB's innovative approach to distributed database systems extends beyond its core architecture to include intelligent client drivers that are specifically designed to interact with distributed clusters. In this section, we will explore the features and benefits of YugabyteDB's cluster-aware [smart client drivers](../../../drivers-orms/smart-drivers/), highlighting their significance in distributed application development.

## External load balancer

Applications typically connect to a multi-node system via a load balancer, which accepts the multiple connections from the client and distributes them across the different nodes in the system. This is also the case for an application connecting to a YugabyteDB cluster using a standard PostgreSQL driver.

![Using an external load balancer](/images/explore/scalability/node-addition-loadbalancer.png)

Although the load balancer hides the cluster from the application, neither the load balancer nor the application has any idea of the topology of the cluster - every time you add a node to the cluster, you need to update the load balancer configuration. The load balancer also adds a network hop for the application to talk to the cluster.

Smart drivers simplify the process of connecting to and communicating with a database cluster by allowing applications to connect without the need for an external load balancer.

## Cluster information

When an application connects to a cluster using the smart driver, the driver makes an initial connection to a node specified in the connection string. The driver then fetches information about all the nodes in the universe using the `yb_servers()` function. This list is refreshed at regular intervals (the default is 5 minutes). Subsequently, the driver connects to the least-loaded node before returning the connection to the application.

## Load balancing

Now that the driver has fetched information about all the nodes in the cluster, if load balancing is enabled via the connection string option `load_balance=true`, the driver distributes new connection requests made by the application across the different nodes in the cluster. In addition, because the application connects directly to the node, there is no extra hop.

![Using an external load balancer](/images/explore/scalability/node-addition-smart-driver.png)

## Learn more

- [Develop with smart drivers](../../../drivers-orms/smart-drivers/)
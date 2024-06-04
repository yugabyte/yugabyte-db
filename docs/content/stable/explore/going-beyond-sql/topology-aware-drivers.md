---
title: Topology aware client drivers
headerTitle: Topology-aware client drivers
linkTitle: Topology-aware drivers
headcontent:
menu:
  stable:
    identifier: topology-aware-drivers
    parent: going-beyond-sql
    weight: 500
rightNav:
  hideH3: true
type: docs
---

Topology-aware connections in YugabyteDB refer to a feature of the [smart client drivers](../../../drivers-orms/smart-drivers/) that enables them to dynamically adapt to the topology (structure and arrangement) of the database cluster. This capability allows the client drivers to make intelligent decisions about which nodes to connect to based on the current state of the cluster, such as node availability. In this section, we will explore the features and benefits of YugabyteDB's topology-aware [smart client drivers](../../../drivers-orms/smart-drivers/), highlighting their significance in distributed application development.

## Specifying topology

To create a topology-aware connection, you specify topology keys in the connection string of the application. Topology keys are a comma-separated list of cluster locations, in the form `cloud.region.zone`, that allow you to limit connections to specific regions and zones. For example, to connect only to nodes in the `us-east-1a` zone located in the `us-east-1` region, you can set your connection string as follows:

```sql
"postgres://host:5433/dbname?topology_keys=aws.us-east-1.us-east-1a"
```

To connect to _any_ node in region `us-east-1`, you can use an asterisk (`*`) for globbing as follows:

```sql
"postgres://host:5433/dbname?topology_keys=aws.us-east-1.*"
```

Using topology keys, you can tell your application to only connect to nodes that are nearby. For example, for applications running in `us-central-1`, you can set `topology_keys` to `aws.us-central-1.*`; for applications running in `us-east`, you can set `topology_keys` to `aws.us-east-1.*`.

![Topology aware setup](/images/explore/smart-driver-setup.png)

## Specifying fallback

When you create a YugabyteDB cluster, you can specify [preferred regions](../../../explore/multi-region-deployments/synchronous-replication-ysql/#preferred-region), which defines the priority of placement of leaders. You can opt to specify the same order of priority in the `topology_keys` parameter to ensure that the driver connects to the next preferred region when the first preferred region fails. For example, if you have set up your first preferred region to `us-east` and your second preferred region to `us-central`, then you can specify the `topology_keys` as follows:

```sql
"postgres://host:5433/dbname?topology_keys=aws.us-east-1.*:1,aws.us-central-1.*:2"
```

This way, if the region `us-east` fails, your application will automatically connect to `us-central`.

![Topology aware setup](/images/explore/smart-driver-failover.png)

## Learn more

- [Develop with smart drivers](../../../drivers-orms/smart-drivers/)
---
title: Cluster topology
headerTitle: Cluster topology
linkTitle: Cluster topology
headcontent: List the servers in your cluster
menu:
  stable:
    identifier: show-cluster-topology
    parent: going-beyond-sql
    weight: 400
rightNav:
  hideH3: true
type: docs
---

In YugabyteDB, the cluster topology refers to the physical and logical layout of the nodes that make up the distributed system. Understanding and choosing the right cluster topology is essential for designing, deploying, and maintaining your database effectively. Topology directly impacts the scalability, performance, fault tolerance, and reliability of the database and thereby your applications.

## List the nodes

You can use the `yb_servers()` function to return a list of all the nodes in your cluster and their location.

{{<note>}}
The [YugabyteDB smart drivers](../../../drivers-orms/smart-drivers) use the `yb_servers()` function to retrieve the cluster information to be [cluster-aware](../cluster-aware-drivers) and [topology-aware](../topology-aware-drivers).
{{</note>}}

The function returns the following information.

|      Name       |                            Description                            |
| --------------: | :---------------------------------------------------------------- |
|            host | Internal IP address of the node.                                   |
|            port | Port at which the service will accept connections.                 |
| num_connections | Number of active connections to the node.                         |
|       node_type | Type of the node. One of `primary`, `readreplica`                 |
|           cloud | Name of cloud provider. For example, `aws`, `gcp`.                    |
|          region | Name of the region in which the node is located. For example, `us-east-1`. |
|            zone | Name of the zone in which the node is located. For example, `us-east-1a`.  |
|       public_ip | Externally accessible public IP address of the node.              |
|            uuid | A UUID that uniquely identifies the node.                          |

## Example

For a 6-node cluster spread across 3 zones in `aws.west`, you should see an output similar to the following:

```output
   host    | port | cxn | node_type | cloud | region |  zone  | public_ip |               uuid
-----------+------+-----------------+-----------+-------+--------+--------+-----------+----------------------
 127.0.0.1 | 5433 |   0 | primary   | aws   | west   | zone-a | 127.0.0.1 | d42d033f334242b2becbc43e028f64a2
 127.0.0.2 | 5433 |   0 | primary   | aws   | west   | zone-b | 127.0.0.2 | 8ab7569823c24d38a48ca2cd1e32ea18
 127.0.0.3 | 5433 |   0 | primary   | aws   | west   | zone-c | 127.0.0.3 | bf07b83fe52c479694d127948718dff3
 127.0.0.4 | 5433 |   0 | primary   | aws   | west   | zone-a | 127.0.0.4 | 7e4abfe19eb74c18a352fbfe99168372
 127.0.0.5 | 5433 |   0 | primary   | aws   | west   | zone-b | 127.0.0.5 | e3680e180e7046f287476fea45feef5e
 127.0.0.6 | 5433 |   0 | primary   | aws   | west   | zone-c | 127.0.0.6 | 80f5eb8622104851b7871a86fc66b5a5
```

## Learn more

- [Cluster-aware client drivers](../cluster-aware-drivers)
- [Topology-aware client drivers](../topology-aware-drivers)
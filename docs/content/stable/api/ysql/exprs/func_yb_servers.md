---
title: yb_servers() function [YSQL]
headerTitle: yb_servers()
linkTitle: yb_servers()
description: Returns a list of all the nodes in your cluster and their location.
menu:
  stable_api:
    identifier: api-ysql-exprs-yb_servers
    parent: api-ysql-exprs
    weight: 10
type: docs
---

## Synopsis

`yb_servers()` is a function that returns a list of all the nodes in your cluster and their location.

## Instructions

The function returns the following information.

|      Name       |                            Description                            |
| :-------------- | :---------------------------------------------------------------- |
|            host | Internal IP address of the node.                                   |
|            port | Port at which the service will accept connections.                 |
| num_connections | Number of active connections to the node.                         |
|       node_type | Type of the node. One of `primary`,`readreplica` .               |
|           cloud | Name of cloud provider. For example, `aws`, `gcp`.                    |
|          region | Name of the region in which the node is located. For example, `us-east-1`. |
|            zone | Name of the zone in which the node is located. For example, `us-east-1a`.  |
|       public_ip | Externally accessible public IP address of the node.              |
|            uuid | A UUID that uniquely identifies the node.                          |

## Example

For a 6-node cluster spread across 3 zones in `aws.west`, you should see an output similar to the following:

```sql
SELECT * FROM yb_servers();
```

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

## Which server am I connected to?

`yb_servers()` exposes all nodes, with their placement, and PostgreSQL `inet_server_port()` exposes the server endpoint you are connected to, so joining both:

```sql
with yb_servers as (
       select host,port,cloud,region,zone from yb_servers()
   ), my_connection as (
     select host(inet_server_addr()) host, inet_server_port() port ,'<<- you are here' as inet_server)
select * from yb_servers natural left join my_connection;
```

```output

      host      | port | cloud |   region   |    zone     |   inet_server
----------------+------+-------+------------+-------------+------------------
 172.129.25.209 | 5433 | aws   | eu-west-1  | eu-west-1a  | <<- you are here
 172.125.42.26  | 5433 | aws   | ap-south-1 | ap-south-1b |
 172.121.27.101 | 5433 | aws   | us-east-2  | us-east-2a  |

(3 rows)
```

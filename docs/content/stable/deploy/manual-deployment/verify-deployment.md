---
title: Verify deployment
headerTitle: Verify deployment
linkTitle: 5. Verify deployment
description: Verify deployment of your YugabyteDB cluster
menu:
  stable:
    identifier: deploy-manual-deployment-verify-deployment
    parent: deploy-manual-deployment
    weight: 615
type: docs
---

You now have a cluster/universe on six nodes with a replication factor of `3`. Assume their IP addresses are `172.151.17.130`, `172.151.17.220`, `172.151.17.140`, `172.151.17.150`, `172.151.17.160`, and `172.151.17.170`. YB-Master servers are running on only the first three of these nodes.

## [Optional] Set up YEDIS API

While the YCQL and YSQL APIs are turned on by default after all of the YB-TServers start, the Redis-compatible YEDIS API is off by default. If you want this cluster to be able to support Redis clients, run the following command from any of the 4 instances. The command adds the special Redis table into the DB and starts the YEDIS server on port 6379 on all instances.

```sh
$ ./bin/yb-admin --master_addresses 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 setup_redis_table
```

{{< note title="Note" >}}

If you want this cluster to be able to support Redis clients, you **must** perform this step.

{{< /note >}}

## View the master UI dashboard

You should now be able to view the master dashboard on the IP address of any master. In this example, this is one of the following URLs:

- `http://172.151.17.130:7000`
- `http://172.151.17.220:7000`
- `http://172.151.17.140:7000`

{{< tip title="Tip" >}}

If this is a public cloud deployment, remember to use the public IP for the nodes, or a HTTP proxy to view these pages.

{{< /tip >}}

## Connect clients

Clients can connect to YSQL API at the following addresses:

```sh
172.151.17.130:5433,172.151.17.220:5433,172.151.17.140:5433,172.151.17.150:5433,172.151.17.160:5433,172.151.17.170:5433
```

Clients can connect to YCQL API at the following addresses:

```sh
172.151.17.130:9042,172.151.17.220:9042,172.151.17.140:9042,172.151.17.150:9042,172.151.17.160:9042,172.151.17.170:9042
```

Clients can connect to YEDIS API at the following addresses:

```sh
172.151.17.130:6379,172.151.17.220:6379,172.151.17.140:6379,172.151.17.150:6379,172.151.17.160:6379,172.151.17.170:6379
```

## Default ports reference

The preceding deployment uses the following default ports:

Service | Type | Port
--------|------| -------
`yb-master` | RPC | 7100
`yb-master` | Admin web server | 7000
`yb-tserver` | RPC | 9100
`yb-tserver` | Admin web server | 9000
`ycql` | RPC | 9042
`ycql` | Admin web server | 12000
`yedis` | RPC | 6379
`yedis` | Admin web server | 11000
`ysql` | RPC | 5433
`ysql` | Admin web server | 13000

For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports/).

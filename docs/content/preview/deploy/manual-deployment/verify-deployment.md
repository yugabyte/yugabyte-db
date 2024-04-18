---
title: Verify manual deployment
headerTitle: Verify deployment
linkTitle: 5. Verify deployment
description: How to verify the manual deployment of the YugabyteDB database cluster.
menu:
  preview:
    identifier: deploy-manual-deployment-verify-deployment
    parent: deploy-manual-deployment
    weight: 615
type: docs
---

You now have a cluster/universe on six nodes with a replication factor of `3`. Assume their IP addresses are `172.151.17.130`, `172.151.17.220`, `172.151.17.140`, `172.151.17.150`, `172.151.17.160`, and `172.151.17.170`. YB-Master servers are running on only the first three of these nodes.

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
`ysql` | RPC | 5433
`ysql` | Admin web server | 13000

For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports/).

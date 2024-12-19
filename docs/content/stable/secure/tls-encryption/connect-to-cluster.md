---
title: Connect clients to YugabyteDB clusters
headerTitle: Connect to clusters
linkTitle: Connect to clusters
description: Connect clients to remote YugabyteDB clusters that have client-to-server encryption enabled.
headcontent: Connect clients to remote YugabyteDB clusters that have client-to-server encryption enabled
menu:
  stable:
    identifier: connect-to-cluster
    parent: tls-encryption
    weight: 40
type: docs
---

You can connect CLIs, tools, and APIs to a remote YugabyteDB cluster when client-to-server encryption is enabled.

## Prerequisites

In order to connect to your YugabyteDB clusters using encryption in transit, you have to enable client-to-server encryption and might need to enable server-to-server encryption (see [Connecting to a YugabyteDB Cluster](#connecting-to-a-yugabytedb-cluster)).

Each client that connects to a YugabyteDB cluster needs the following file to be accessible on the client computer:

- `ca.crt` — root certificate file (for YSQL and YCQL). See [Generate the root certificate file](../server-certificates/#generate-the-root-certificate-file) for instructions on how to generate this file.

  This file should be available in the `~/.yugabytedb`, the default location for TLS certificates when running the YSQL shell (`ysqlsh`) locally.

## Connecting to a YugabyteDB Cluster

For each client, the steps assume that you have performed the following:

- [Enabled client-to-server encryption](../client-to-server/) on the YB-TServer nodes of your YugabyteDB cluster.
- [Enabled server-to-server encryption](../server-to-server/) on the YugabyteDB cluster.

## ysqlsh

The `ysqlsh` CLI is available in the `bin` directory of your YugabyteDB home directory.

To connect to a remote YugabyteDB cluster, you need to have a local copy of `ysqlsh` available. You can use the `ysqlsh` CLI available on a locally installed YugabyteDB.

To open the local `ysqlsh` CLI and access your YugabyteDB cluster, run `ysqlsh` with the following flags defined:

- host: `-h <node-ip-address>` (required for remote node; default is `127.0.0.1`)
- port: `-p <port>` (optional; default is `5433`)
- user: `-U <username>` (optional; default is `yugabyte`)
- TLS/SSL: `"sslmode=require"` (required)

```sh
$ ./bin/ysqlsh -h 127.0.0.1 -p 5433 -U yugabyte "sslmode=require"
```

```output
ysqlsh (11.2-YB-{{<yb-version version="stable">}}-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.

yugabyte=#
```

## yb-admin

To enable yb-admin to connect with a cluster having TLS enabled, pass in the extra argument of `certs_dir_name` with the directory location where the root certificate is present. The yb-admin tool is present on the cluster node in the `~/master/bin/` directory. The `~/yugabyte-tls-config` directory on the cluster node contains all the certificates.

For example, the following command lists the master information for the TLS-enabled cluster:

```sh
export MASTERS=node1:7100,node2:7100,node3:7100
./bin/yb-admin --master_addresses $MASTERS -certs_dir_name ~/yugabyte-tls-config list_all_masters
```

You should see the following output format:

```output
Master UUID RPC Host/Port State Role
UUID_1    node1:7100      ALIVE FOLLOWER
UUID_2    node2:7100      ALIVE LEADER
UUID_3    node3:7100      ALIVE FOLLOWER
```

## ycqlsh

To enable `ycqlsh` to connect to a YugabyteDB cluster with encryption enabled, you need to set the following environment variables:

Variable       | Description
---------------|------------------------------
`SSL_CERTFILE` | The root certificate file (`ca.crt`).

To set the environment variables, use the following `export` commands:

```sh
$ export SSL_CERTFILE=<path to file>/ca.crt
```

The next step is to connect using the `--ssl` flag.

### Local Cluster

```sh
$ ./bin/ycqlsh --ssl
```

You should see the following output:

```output
Connected to local cluster at X.X.X.X:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```

### Remote Cluster

To connect to a remote YugabyteDB cluster, you need to have a local copy of `ycqlsh` available. You can use the `ycqlsh` CLI available on a locally-installed YugabyteDB.

To open the local `ycqlsh` CLI and access the remote cluster, run `ycqlsh` with flags set for the host and port of the remote cluster. You must also add the `--ssl` flag to enable the use of the client-to-server encryption using TLS (successor to SSL), as follows:

```sh
$ ./bin/ycqlsh <node-ip-address> <port> --ssl
```

- *node-ip-address*: the IP address of the remote node.
- *port*: the port of the remote node.

For example, if the host is `127.0.0.2`, the port is `9042`, and the user is `yugabyte`, run the following command to connect:

```sh
$ ./bin/ycqlsh 127.0.0.2 9042 --ssl
```

You should see the following output:

```output
Connected to local cluster at X.X.X.X:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
ycqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```

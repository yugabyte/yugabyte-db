---
title: 4. Connect to cluster
linkTitle: 4. Connect to cluster
description: 4. Connect to cluster
headcontent: Connect to YugabyteDB cluster using cqlsh.
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
aliases:
  - /secure/tls-encryption/connect-to-cluster
menu:
  latest:
    identifier: connect-to-cluster
    parent: tls-encryption
    weight: 40
isTocNested: true
showAsideToc: true
---

To use CLIs, tools, and APIs on a remote YugabyteDB cluster with encryption enabled, you need to generate client configuration files that enable the client to connect to YugabyteDB. Follow the steps in the [Generate node configurations](../prepare-nodes/#generate-per-node-config) to create the files needed

You need the following three files to be accessible on the client computer:

* `rootCA.crt` — see [Prepare root configuration file](../prepare-nodes/#generate-root-config)
* `node.<node-ip-address>.crt` — see [Generate private key for each node](../prepare-nodes/#generate-private-key-for-each-node)
* `node.<node-ip-address>.key` — see [Generate private key for each node](../prepare-nodes/#generate-private-key-for-each-node)

All three of the files should be placed in the `~/.yugabytedb`, which is the default location for TLS certificates when running the YSQL shell (`ysqlsh`) locally.

## ysqlsh

To use the YSQL shell (`ysqlsh`) to connect with a remote YugabyteDB cluster that has client-server encryption enabled, you need to include the `--certs_dir_name` option, specifying the directory location where the root certificate is present. The `ysqlsh` tool, located in the  is present on the cluster node in the `~/master/bin/` directory. The `~/yugabyte-tls-config` directory on the cluster nodes contains all TLS certificates.

```sh
$ ./bin/ysqlsh -h 127.0.0.1 -p 5433 -U yugabyte 
```

```sh
ysqlsh (11.2-YB-2.0.0.0-b0)
Type "help" for help.

yugabyte=#
```

## yb-admin

To enable `yb-admin` to connect with a cluster having TLS enabled, pass in the extra argument of `certs_dir_name` with the directory location where the root certificate is present. The `yb-admin` tool is present on the cluster node in the `~/master/bin/` directory. The `~/yugabyte-tls-config` directory on the cluster node contains all the certificates.

For example, the command below will list the master information for the TLS enabled cluster:

```sh
export MASTERS=node1:7100,node2:7100,node3:7100
./bin/yb-admin --master_addresses $MASTERS -certs_dir_name ~/yugabyte-tls-config list_all_masters
```

You should see the following output format:

```sh
Master UUID	RPC Host/Port	State	Role
UUID_1 		node1:7100  	ALIVE 	FOLLOWER
UUID_2		node2:7100     	ALIVE 	LEADER
UUID_3 		node3:7100     	ALIVE 	FOLLOWER
```

## cqlsh

To enable `cqlsh` to connect, set the following environment variables:

Variable       | Description                  |
---------------|------------------------------|
`SSL_CERTFILE` | The root certificate file (`ca.crt`). |
`SSL_USERCERT` | The user certificate file  (`node.<name>.crt`). |
`SSL_USERKEY`  | The user key file (`node.<name>.key`).  |

You can do so by doing the following:

```sh
$ export SSL_CERTFILE=<path to file>/ca.crt
$ export SSL_USERCERT=<path to file>/node.<name>.crt
$ export SSL_USERKEY=<path to file>/node.<name>.key
```

Next connect using the `--ssl` flag.

```sh
$ ./bin/cqlsh --ssl
```

You should see the following output:

```sql
Connected to local cluster at X.X.X.X:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```

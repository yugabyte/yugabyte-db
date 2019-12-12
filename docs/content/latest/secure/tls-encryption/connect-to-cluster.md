---
title: 4. Connect to cluster
linkTitle: 4. Connect to cluster
description: 4. Connect to cluster
headcontent: Connect to YugabyteDB cluster using cqlsh and yb-admin.
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
aliases:
  - /secure/tls-encryption/connect-to-cluster
menu:
  latest:
    identifier: connect-to-cluster
    parent: tls-encryption
    weight: 744
isTocNested: true
showAsideToc: true
---

You would need to generate client configuration files to enable the client to connect to YugabyteDB. The steps are identical to [preparing the per-node configuration](../prepare-nodes/#generate-per-node-config) shown in a previous section.

You would need the following files on the client node:

* `ca.crt` as described in the [prepare config](../prepare-nodes/#generate-root-config) section
* `node.<name>.crt` as described in the [node config](../prepare-nodes/#generate-private-key-for-each-node) section
* `node.<name>.key` as shown in the [node config](../prepare-nodes/#generate-private-key-for-each-node) section

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

```
Connected to local cluster at X.X.X.X:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system
```

## yb-admin

To enable `yb-admin` to connect with a cluster with TLS enabled, you can pass in the extra argument of `certs_dir_name` with the directory location where the root certificate is present. The `yb-admin` tool is present on the cluster node in the `~/master/bin/` directory. The `~/yugabyte-tls-config` directory on the cluster node contains all the certificates.

For example, the command below will list the master information for the TLS-enabled cluster:

```sh
export MASTERS=node1:7100,node2:7100,node3:7100
./yb-admin --master_addresses $MASTERS -certs_dir_name ~/yugabyte-tls-config list_all_masters
```

You should see the following output format:

```
Master UUID	RPC Host/Port	State	Role
UUID_1 		node1:7100  	ALIVE 	FOLLOWER
UUID_2		node2:7100     	ALIVE 	LEADER
UUID_3 		node3:7100     	ALIVE 	FOLLOWER
```

For details on the `yb-admin` utility, see [yb-admin](../../../admin/yb-admin).

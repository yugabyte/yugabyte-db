---
title: Enable server-to-server encryption
headerTitle: Enable server-to-server encryption
linkTitle: Server-to-server encryption
description: Enable server-to-server encryption (using TLS) for intranode communication.
headcontent: Enable server-to-server encryption (using TLS) for intranode communication
menu:
  v2024.1:
    identifier: server-to-server
    parent: tls-encryption
    weight: 20
type: docs
---

## Prerequisites

Before you can enable and use server-to-server encryption, you need to create and configure server certificates for each node of your YugabyteDB cluster. For information, see [Create server certificates](../server-certificates).

## Configure YB-Master and YB-TServer nodes

To enable server-to-server encryption using TLS, start your YB-Master and YB-TServer nodes using the following flags.

Flag                           | Node                  | Description                  |
-------------------------------|--------------------------|------------------------------|
`use_node_to_node_encryption`  | YB-Master, YB-TServer | Set to `true` to enable encryption between YugabyteDB nodes. Default value is `false`. |
`allow_insecure_connections`   | YB-Master, YB-TServer | Set to `false` to disallow any service with unencrypted communication from joining this cluster. Default value is `true`. Note that this flag requires `--use_node_to_node_encryption` to be enabled. |
`certs_dir`                    | YB-Master, YB-TServer | Optional. Directory containing the certificates created for this node to perform encrypted communication with the other nodes. See [Create server certificates](../server-certificates). Default for YB-Masters is `<data drive>/yb-data/master/data/certs` and for YB-TServers is `<data drive>/yb-data/tserver/data/certs`. |

## Start the YB-Masters

You can enable access control by starting the `yb-master` services with the `--use_node_to_node_encryption=true` flag as described above. Your command should look similar to this:

```sh
bin/yb-master                               \
    --fs_data_dirs=<data directories>       \
    --master_addresses=<master addresses>   \
    --certs_dir=/home/centos/tls/$NODE_IP   \
    --allow_insecure_connections=false      \
    --use_node_to_node_encryption=true
```

For information on starting YB-Master nodes for a deployment, see [Start YB-Masters](../../../deploy/manual-deployment/start-masters/).

## Start the YB-TServers

You can enable access control by starting the `yb-tserver` services using the `--use_node_to_node_encryption=true` flag described above. Your command should look similar to this:

```sh
bin/yb-tserver                                  \
    --fs_data_dirs=<data directories>           \
    --tserver_master_addrs=<master addresses>   \
    --certs_dir /home/centos/tls/$NODE_IP       \
    --use_node_to_node_encryption=true &
```

For information on starting YB-TServers for a deployment, see [start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

## Connect to the cluster

Because you have only enabled server-to-server encryption and not [client-to-server encryption](../client-to-server/), you can now connect to this cluster using the YSQL shell (`ysqlsh`) or the YCQL shell (`ycqlsh`) without enabling encryption as shown here.

### YSQL

```sh
$ ./bin/ysqlsh
```

```output
ysqlsh (11.2-YB-2.0.11.0-b0)
Type "help" for help

yugabyte=#
```

### YCQL

```sh
$ ./bin/ycqlsh
```

```output
Connected to local cluster at 127.0.0.1:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.

ycqlsh>
```

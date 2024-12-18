---
title: Enable client-to-server encryption
headerTitle: Enable client-to-server encryption
linkTitle: Client-to-server encryption
description: Enable client-to-server encryption (using TLS) for YSQL and YCQL.
headcontent: Enable client-to-server encryption (using TLS) for YSQL and YCQL
aliases:
  - /secure/tls-encryption/client-to-server
menu:
  preview:
    identifier: client-to-server
    parent: tls-encryption
    weight: 30
type: docs
---

You can configure YugabyteDB clusters to use client-to-server encryption to protect data in transit between YugabyteDB servers and clients, tools, and APIs. When enabled, [Transport Layer Security (TLS)](https://en.wikipedia.org/wiki/Transport_Layer_Security) (the successor to the deprecated Secure Sockets Layer (SSL)) is used to ensure data protection for YSQL and YCQL.

## Prerequisites

Before you can enable client-to-server encryption, you first must create server certificates for each node of your YugabyteDB cluster. For information, see [Create server certificates](../server-certificates).

## Configure YB-Master and YB-TServer nodes

To enable client-to-server encryption for YSQL and YCQL, start your YB-Master and YB-TServer nodes using the following flags.

| Configuration flag              | Description                    |
| --------------------------------| ------------------------------ |
| use_client_to_server_encryption |  Set to `true` to enable encryption between the various YugabyteDB clients and the database cluster. Default is `false`. |
| allow_insecure_connections      | Set to `false` to disallow any client with unencrypted communication from joining this cluster. Default is `true`. Note that this flag requires `--use_client_to_server_encryption` to be enabled. |
| certs_for_client_dir            | Optional. Defaults to the same directory as the server-to-server encryption. Directory containing the configuration for the client to perform TLS communication with the cluster. Default for YB-Masters is `<data drive>/yb-data/master/data/certs` and for YB-TServer is `<data drive>/yb-data/tserver/data/certs`. |

To enable access control, start the yb-master and yb-tserver services with the following flag:

```output
--use_client_to_server_encryption=true
```

This flag enables both encrypted and unencrypted clients to connect to the cluster.

To prevent clients without the appropriate encryption from connecting, you must add the following flag:

```output
--allow_insecure_connections=false
```

Your command for the YB-Master should look similar to this:

```sh
bin/yb-master                                        \
    --fs_data_dirs=<data directories>                \
    --master_addresses=<master addresses>            \
    --certs_for_client_dir /home/centos/tls/$NODE_IP \
    --allow_insecure_connections=false               \
    --use_client_to_server_encryption=true
```

Your command for the YB-TServer should look similar to this:

```sh
bin/yb-tserver                                       \
    --fs_data_dirs=<data directories>                \
    --tserver_master_addrs=<master addresses>        \
    --certs_for_client_dir /home/centos/tls/$NODE_IP \
    --allow_insecure_connections=false               \
    --use_client_to_server_encryption=true
```

For information on starting YB-Master nodes for a deployment, see [Start YB-Masters](../../../deploy/manual-deployment/start-masters/).

For information on starting YB-TServers for a deployment, see [start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

---
title: Enable encryption in transit
headerTitle: Enable encryption in transit
linkTitle: Enable encryption in transit
description: Enable encryption (using TLS) for client- and server-server communication.
headcontent: Enable encryption (using TLS) for client- and server-server communication
menu:
  v2.25:
    identifier: server-to-server
    parent: tls-encryption
    weight: 20
type: docs
---

## Prerequisites

Before you can enable and use server-to-server (inter node) and client-to-server encryption, you need to create and configure server certificates for each node of your YugabyteDB cluster. For information, see [Create server certificates](../server-certificates).

## Configure YB-Master and YB-TServer nodes

To enable server-to-server and client-to-server TLS encryption, start your YB-Master and YB-TServer nodes using the following flags.

| Configuration flag          | Description                  |
| --------------------------- | ---------------------------- |
| use_node_to_node_encryption | Set to `true` to enable encryption between YugabyteDB nodes. Default is `false`. |
| use_client_to_server_encryption |  Set to `true` to enable encryption between clients and the database cluster. Default is `false`. |
| allow_insecure_connections  | Set to `false` to disallow any service with unencrypted communication from joining this cluster. Default is `true`. Note that this flag requires `--use_node_to_node_encryption` or `--use_client_to_server_encryption` to be enabled. |
| certs_dir                   | Optional. Directory containing the certificates created for this node to perform encrypted communication with the other nodes. Default for YB-Masters is `<data drive>/yb-data/master/data/certs` and for YB-TServers is `<data drive>/yb-data/tserver/data/certs`. |
| certs_for_client_dir        | Optional. Directory containing the configuration for the client to perform TLS communication with the cluster. Defaults to the same directory as the node-to-node encryption (certs_dir). |

## Start the YB-Masters

You can enable encryption in transit by starting the yb-master services with the following flags:

```sh
bin/yb-master                               \
    --fs_data_dirs=<data directories>       \
    --master_addresses=<master addresses>   \
    --certs_dir=/home/centos/tls/$NODE_IP   \
    --allow_insecure_connections=false      \
    --use_node_to_node_encryption=true      \
    --use_client_to_server_encryption=true
```

For information on starting YB-Master nodes for a deployment, see [Start YB-Masters](../../../deploy/manual-deployment/start-masters/).

## Start the YB-TServers

You can enable encryption in transit by starting the yb-tserver services with the following flags:

```sh
bin/yb-tserver                                  \
    --fs_data_dirs=<data directories>           \
    --tserver_master_addrs=<master addresses>   \
    --certs_dir /home/centos/tls/$NODE_IP       \
    --allow_insecure_connections=false          \
    --use_node_to_node_encryption=true          \
    --use_client_to_server_encryption=true
```

For information on starting YB-TServers for a deployment, see [start YB-TServers](../../../deploy/manual-deployment/start-masters/#yb-tserver-servers).

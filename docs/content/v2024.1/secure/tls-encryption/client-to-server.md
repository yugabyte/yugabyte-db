---
title: Enable client-to-server encryption
headerTitle: Enable client-to-server encryption
linkTitle: Client-to-server encryption
description: Enable client-to-server encryption (using TLS) for YSQL and YCQL.
headcontent: Enable client-to-server encryption (using TLS) for YSQL and YCQL
menu:
  v2024.1:
    identifier: client-to-server
    parent: tls-encryption
    weight: 30
type: docs
---

YugabyteDB clusters can be configured to use client-to-server encryption to protect data in transit between YugabyteDB servers and clients, tools, and APIs. When enabled, [Transport Layer Security (TLS)](https://en.wikipedia.org/wiki/Transport_Layer_Security), the successor to the deprecated Secure Sockets Layer (SSL), is used to ensure data protection for YSQL and YCQL only.

## Prerequisites

Before you can enable client-to-server encryption, you first must [create server certificates](../server-certificates/).

## Configure YB-TServer nodes

To enable client-to-server encryption for YSQL and YCQL, start your YB-TServer services with the required flags described below. Your YB-Master services do not require additional configuration.

Configuration flag                   | Process    | Description                  |
-------------------------------------|------------|------------------------------|
[`--use_client_to_server_encryption`](../../../reference/configuration/yb-tserver/#use-client-to-server-encryption) | YB-TServer | Set to `true` to enable encryption between the various YugabyteDB clients and the database cluster. Default value is `false`. |
[`--allow_insecure_connections`](../../../reference/configuration/yb-tserver/#allow-insecure-connections) | YB-TServer | Set to `false` to disallow any client with unencrypted communication from joining this cluster. Default value is `true`. Note that this flag requires `--use_client_to_server_encryption` to be enabled. |
[`--certs_for_client_dir`](../../../reference/configuration/yb-tserver/#certs-for-client-dir) | YB-TServer | Optional. Defaults to the same directory as the server-to-server encryption. This directory should contain the configuration for the client to perform TLS communication with the cluster. Default value for YB-TServer is `<data drive>/yb-data/tserver/data/certs` |

To enable access control, follow these steps, start the `yb-tserver` services with the following flag (described above):

```output
--use_client_to_server_encryption=true
```

This flag enables both encrypted and unencrypted clients to connect to the cluster.

To prevent clients without the appropriate encryption from connecting, you must add the following flag:

```output
--allow_insecure_connections=false
```

Your command should look similar to this:

```output
bin/yb-tserver                                       \
    --fs_data_dirs=<data directories>                \
    --tserver_master_addrs=<master addresses>        \
    --certs_for_client_dir /home/centos/tls/$NODE_IP \
    --allow_insecure_connections=false               \
    --use_client_to_server_encryption=true &
```

For details about starting YB-TServer nodes in manual deployments, see [Start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

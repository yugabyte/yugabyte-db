---
title: Enable client-to-server encryption
linkTitle: Enable client-to-server encryption
description: Enable client-to-server encryption
headcontent: Enable client-to-server encryption (using TLS) for YSQL and YCQL.
image: /images/section_icons/secure/tls-encryption/client-to-server.png
aliases:
  - /secure/tls-encryption/client-to-server
menu:
  latest:
    identifier: client-to-server
    parent: tls-encryption
    weight: 30
isTocNested: true
showAsideToc: true
---

YugabyteDB can be configured to provide client-to-server encryption, using Transport Layer Security (TLS), for YSQL and YCQL. Note that there is no planned support for YEDIS.

## Prerequisites

Before you can enable and use server-to-server encryption, you need to create and configure server certificates for each node of your YugabyteDB cluster. For information, see [Create server certificates](../server-certificates).

## Configure YB-TServer nodes

To enable client-to-server encryption (using TLS) for YSQL and YCQL, start your YB-TServer services with the required options described below. Your YB-Master services do not require additional configuration.

Configuration option (flag)          | Process    | Description                  |
-------------------------------------|------------|------------------------------|
[`--use_client_to_server_encryption`](../../../admin/yb-tserver/#use-client-to-server-encryption)  | YB-TServer | Set to `true` to enable encryption between the various YugabyteDB clients and the database cluster. Default value is `false`. |
[`--allow_insecure_connections`](../../../admin/yb-tserver/#allow-insecure-connections)         | YB-TServer | Set to `false` to disallow any client with unencrypted communication from joining this cluster. Default value is `true`. Note that this option requires `--use_client_to_server_encryption` to be enabled. |
[`--certs_for_client_dir`](../../../admin/yb-tserver/#certs-for-client-dir)               | YB-TServer | Optional. Defaults to the same directory as the server-to-server encryption. This directory should contain the configuration for the client to perform TLS communication with the cluster. Default value for YB-TServer is `<data drive>/yb-data/tserver/data/certs`  |

To enable access control, follow these steps, start the `yb-tserver` services with the following option (described above):
  
```
--use_client_to_server_encryption=true`
```

This option enables both encrypted and unencrypted clients to connect to the cluster.

To prevent clients without the appropriate encryption from connecting, you must add the following option:

```
--allow_insecure_connections=false`
```

Your command should look similar to this:

```
bin/yb-tserver                                       \
    --fs_data_dirs=<data directories>                \
    --tserver_master_addrs=<master addresses>        \
    --certs_for_client_dir /home/centos/tls/$NODE_IP \
    --allow_insecure_connections=false               \
    --use_client_to_server_encryption=true &
```

For details about starting YB-TServer nodes in manual deployments, see [Start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

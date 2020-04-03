---
title: 3. Client-server encryption
linkTitle: 3. Client-server encryption
description: 3. Client-server encryption
headcontent: Enable client to server encryption.
image: /images/section_icons/secure/tls-encryption/client-to-server.png
block_indexing: true
menu:
  v1.3:
    identifier: secure-tls-encryption-client-to-server
    parent: secure-tls-encryption
    weight: 723
isTocNested: true
showAsideToc: true
---

YugabyteDB can be configured to provide client-server encryption, using Transport Layer Security (TLS), for YSQL and YCQL.

{{< note title="Note" >}}

YEDIS does not include support for client-server TLS encryption.

{{< /note >}}

To enable client-server TLS encryption, start the YB-TServer processes with the required flags described below. The YB-TMaster processes do not require additional configuration.

Flag                                 | Process    | Description                  |
-------------------------------------|--------------------------|------------------------------|
`--use_client_to_server_encryption`  | YB-TServer | [Optional] Set to `true` to enable encryption between the various YugabyteDB clients and the database cluster. Default value is `false`. |
`allow_insecure_connections`         | YB-TServer | [Optional] Set to `false` to disallow any client with unencrypted communication from joining this cluster. Default value is `true`. Note that this flag requires `use_client_to_server_encryption` to be enabled. |
`certs_for_client_dir`               | YB-TServer | [Optional] Defaults to the same directory as the server to server encryption. This directory should contain the configuration for the client to perform TLS communication with the cluster. Default value for the YB-TServers is `<data drive>/yb-data/tserver/data/certs`  |

To enable access control, follow these steps, start the `yb-tserver` processes with the following flag (described above):
  
```
--use_client_to_server_encryption=true`
```

This setting allows both encrypted clients and unencrytped clients to connect to the cluster.

To prevent clients without the appropriate encryption from connecting, you must add the following flag:

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

For details about starting YB-TServers in manual deployments, see [Start YB-TServers](../../../deploy/manual-deployment/start-tservers/).

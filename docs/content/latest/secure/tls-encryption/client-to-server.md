---
title: 3. Client-Server Encryption
linkTitle: 3. Client-Server Encryption
description: 3. Client-Server Encryption
headcontent: Enable client to server encryption.
image: /images/section_icons/secure/tls-encryption/client-to-server.png
aliases:
  - /secure/tls-encryption/client-to-server
menu:
  latest:
    identifier: secure-tls-encryption-client-to-server
    parent: secure-tls-encryption
    weight: 723
isTocNested: true
showAsideToc: true
---

To enable client to server encryption, bring up the YB-TServer processes with the appropriate flags as shown below.

{{< note title="Note" >}}
Only the `yb-tserver` process requires additional flags to enable client to server encryption.
{{< /note >}}

Flag                                 | Process    | Description                  |
-------------------------------------|--------------------------|------------------------------|
`--use_client_to_server_encryption`  | YB-TServer | Optional, default value is `false`. Set to `true` to enable encryption between the various YugaByte DB clients and the database cluster. |
`allow_insecure_connections`         | YB-TServer | Optional, defaults to `true`. Set to `false` to disallow any client with unencrypted communication from joining this cluster. Default value is `true`. Note that this flag requires the `use_client_to_server_encryption` to be enabled. |
`certs_for_client_dir`               | YB-TServer | Optional, defaults to the same directory as the server to server encryption. This directory should contain the configuration for the client to perform TLS communication with the cluster. Default value for the YB-TServers is `<data drive>/yb-data/tserver/data/certs`  |

You can enable access control by starting the `yb-tserver` processes minimally with the `--use_client_to_server_encryption=true` flag as described above. This will allow both encrypted client and clients without encryption to connect to the cluster. To ensure that only clients with appropriate encryption configured are able to connect, set the `--allow_insecure_connections=false` flag as well.

{{< note title="Note" >}}
Remember to set `--allow_insecure_connections=false` to enforce TLS communication between the YugaByte DB cluster and all the clients. Dropping this flag will allow clients to connect without encryption as well.
{{< /note >}}

Your command should look similar to that shown below:

```
bin/yb-tserver                                       \
    --fs_data_dirs=<data directories>                \
    --tserver_master_addrs=<master addresses>        \
    --certs_for_client_dir /home/centos/tls/$NODE_IP \
    --allow_insecure_connections=false               \
    --use_client_to_server_encryption=true &
```


You can read more about bringing up the YB-TServers for a deployment in the section on [manual deployment of a YugaByte DB cluster](../../../deploy/manual-deployment/start-tservers/).

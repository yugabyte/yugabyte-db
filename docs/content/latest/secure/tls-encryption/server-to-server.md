---
title: 2. Server-server encryption
linkTitle: 2. Server-server encryption
description: 2. Server-server encryption
headcontent: Enable server to server encryption between YB-Masters and YB-TServers.
image: /images/section_icons/secure/tls-encryption/server-to-server.png
aliases:
  - /secure/tls-encryption/server-to-server
menu:
  latest:
    identifier: secure-tls-encryption-server-to-server
    parent: secure-tls-encryption
    weight: 722
---


To enable server to server encryption, bring up the YB-Master and YB-TServer processes with the appropriate flags as shown below.


Flag                           | Process                  | Description                  |
-------------------------------|--------------------------|------------------------------|
`use_node_to_node_encryption`  | YB-Master, YB-TServer | Optional, default value is `false`. Set to `true` to enable encryption between the various YugaByte DB server processes. |
`allow_insecure_connections`   | YB-Master only           | Optional, defaults to `true`. Set to `false` to disallow any process with unencrypted communication from joining this cluster. Default value is `true`. Note that this flag requires the `use_node_to_node_encryption` to be enabled. |
`certs_dir`                    | YB-Master, YB-TServer | Optional. This directory should contain the configuration that was prepared in the a step for this node to perform encrypted communication with the other nodes. Default value for YB-Masters is `<data drive>/yb-data/master/data/certs` and for YB-TServers this location is `<data drive>/yb-data/tserver/data/certs` |


## Start the master process

You can enable access control by starting the `yb-master` processes minimally with the `--use_node_to_node_encryption=true` flag as described above. Your command should look similar to that shown below:

```{.sh .copy .separator-dollar}
bin/yb-master                               \
    --fs_data_dirs=<data directories>       \
    --master_addresses=<master addresses>   \
    --certs_dir=/home/centos/tls/$NODE_IP   \
    --allow_insecure_connections=false      \
    --use_node_to_node_encryption=true
```

You can read more about bringing up the YB-Masters for a deployment in the section on [manual deployment of a YugaByte DB cluster](../../../deploy/manual-deployment/start-masters/).


## Start the tserver process

You can enable access control by starting the `yb-tserver` processes minimally with the `--use_node_to_node_encryption=true` flag as described above. Your command should look similar to that shown below:


```{.sh .copy .separator-dollar}
bin/yb-tserver                                  \
    --fs_data_dirs=<data directories>           \
    --tserver_master_addrs=<master addresses>   \
    --certs_dir /home/centos/tls/$NODE_IP       \
    --use_node_to_node_encryption=true &
```

You can read more about bringing up the YB-TServers for a deployment in the section on [manual deployment of a YugaByte DB cluster](../../../deploy/manual-deployment/start-tservers/).


## Connect to the cluster

Since we have only enabled encryption between the database servers, we should be able to connect to this cluster using `cqlsh` without enabling encryption as shown below.

```{.sh .copy .separator-dollar}
$ ./bin/cqlsh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system

cqlsh>
```

{{< note title="Note" >}}
Since we have not enforced client to server encrypted communication, connecting to this cluster using `cqlsh` without TLS encryption enabled would work.
{{< /note >}}

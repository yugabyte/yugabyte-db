---
title: 4. Connect to cluster
linkTitle: 4. Connect to cluster
description: 4. Connect to cluster
headcontent: Connect to YugaByte DB cluster using cqlsh.
image: /images/section_icons/secure/tls-encryption/connect-to-cluster.png
aliases:
  - /secure/tls-encryption/connect-to-cluster
menu:
  latest:
    identifier: secure-tls-encryption-connect-to-cluster
    parent: secure-tls-encryption
    weight: 724
---

You would need to generate client config files to enable the client to connect to YugaByte DB. The steps are identical to [preparing the per-node configuration](http://localhost:1313/latest/secure/tls-encryption/prepare-nodes/#generate-per-node-config) shown in a previous section.

You would need the following files on the client node:

* `ca.crt` as described in the [prepare config](../../../secure/tls-encryption/prepare-nodes/#generate-root-config) section
* `node.<name>.crt` as described in the [node config](../../../secure/tls-encryption/prepare-nodes/#generate-private-key-for-each-node) section
* `node.<name>.key` as shown in the [node config](../../../secure/tls-encryption/prepare-nodes/#generate-private-key-for-each-node) section


To enable cqlsh to connect, set the following environment variables:

Variable       | Description                  |
---------------|------------------------------|
`SSL_CERTFILE` | The root certificate file (`ca.crt`). |
`SSL_USERKEY`  | The user key file  (`node.<name>.crt`). |
`SSL_USERCERT` | The user certificate file (`node.<name>.key`).  |


You can do so by doing the following:

```{.bash}
$ export SSL_CERTFILE=<path to file>/ca.crt
$ export SSL_USERKEY=<path to file>/node.<name>.crt
$ export SSL_USERKEY=<path to file>/node.<name>.key
```

Next connect using the `--ssl` flag.


```{.sh .copy .separator-dollar}
$ ./bin/cqlsh --ssl
```

You should see the following output:
```
Connected to local cluster at X.X.X.X:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh> DESCRIBE KEYSPACES;

system_schema  system_auth  system

cqlsh>
```



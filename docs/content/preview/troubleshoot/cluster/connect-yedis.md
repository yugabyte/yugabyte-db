---
title: YEDIS API connection issues
linkTitle: YEDIS API connection Issues
description: Cannot connect to YEDIS API
aliases:
  - /troubleshoot/cluster/connect-redis/
  - /preview/troubleshoot/cluster/connect-yedis/
menu:
  preview:
    parent: troubleshoot-cluster
    weight: 824
type: docs
---

To troubleshoot YEDIS API connection issues, you should perform a number of checks.

## Are YugabyteDB processes running?

You should ensure that the expected YugabyteDB processes are on the current node. At a minimum, the YB-TServer process needs to be running to be able to connect to this node with a Redis client or application.

Additionally, depending on the setup, you might expect a master process to also be running on this node.

For detailed instructions, see [Check Processes](../../nodes/check-processes/).

## Is the Redis-compatible YEDIS API running?

If the tserver process is running, execute the following command to verify that the YEDIS API is enabled and listening on the Redis port (default `6379`):

```sh
lsof -i :6379
```

```output
COMMAND     PID   USER   FD   TYPE     DEVICE SIZE/OFF NODE NAME
yb-tserve 81590 centos   92u  IPv4 0xdeadbeef      0t0  TCP localhost:6379 (LISTEN)
```

You may need to install `lsof` first.

When running a local cluster with `yb-ctl`, you should see all the nodes here with different IP addresses, as per the following example:

```sh
lsof -i :6379
```

```output
COMMAND     PID   USER   FD   TYPE     DEVICE SIZE/OFF NODE NAME
yb-tserve 81590 centos   92u  IPv4 0xdeadbeef      0t0  TCP localhost:6379 (LISTEN)
yb-tserve 81593 centos   92u  IPv4 0xdeadbeef      0t0  TCP 127.0.0.2:6379 (LISTEN)
yb-tserve 81596 centos   92u  IPv4 0xdeadbeef      0t0  TCP 127.0.0.3:6379 (LISTEN)
```

If another process is using this port, you might need to stop that and restart the YB-TServer process. Otherwise, if no process is listening but the YB-TServer is running, check the value of the `--cql_proxy_bind_address` flag passed to the YB-TServer process.

## Can redis-cli connect locally?

Use `redis-cli` to connect to the local node, as follows:

```sh
./redis-cli -h <yb-local-address>
```

Depending on your configuration, you may need to install `redis-cli`; otherwise, it is available in the YugabyteDB `bin` directory.

In the preceding command, `<yb-local-address>` is the address where the YEDIS service is listening (for example, as returned by `lsof`).

If `redis-cli` can connect, the issue is likely a network issue with the original client not being able to access this node where YugabyteDB is running. For more information, see [Cannot access Master or TServer endpoints](#cannot-access-master-or-tserver-endpoints).

Otherwise, you might need to run `./yb-admin --master_addresses <master-ip-addresses> setup_redis_table"`. The `yb-admin` tool is available in the YugabyteDB `bin` directory.
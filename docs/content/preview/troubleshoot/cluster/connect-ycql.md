---
title: YCQL API connection issues
linkTitle: YCQL API connection issues
description: Cannot connect to YCQL API
badges: ycql
aliases:
  - /troubleshoot/cluster/connect-cassandra/
menu:
  preview:
    parent: troubleshoot-cluster
    weight: 822
type: docs
---

To troubleshoot YCQL API connection issues, you should perform a number of checks.

## Are YugabyteDB processes running?

You should ensure that the expected YugabyteDB processes are on the current node. At a minimum, the YB-TServer process needs to be running to be able to connect to this node with a YCQL client or application.

Additionally, depending on the setup, you might expect a YB-Master process to also be running on this node.

For detailed instructions, see [Check YugabyteDB servers](../../nodes/check-processes/).

## Is the Cassandra-compatible YCQL API running?

If the YB-TServer process is running, execute the following command to verify that the YCQL API is enabled and listening on the YCQL port (default `9042`):

```sh
lsof -i :9042
```

```output
COMMAND     PID   USER   FD   TYPE     DEVICE SIZE/OFF NODE NAME
yb-tserve 81590 centos  131u  IPv4 0xdeadbeef      0t0  TCP localhost:9042 (LISTEN)
```

You may need to install `lsof` first.

When running a local cluster with yb-ctl, you should see all the nodes with different IP addresses, as per the following example:

```sh
lsof -i :9042
```

```output
COMMAND     PID   USER   FD   TYPE     DEVICE SIZE/OFF NODE NAME
yb-tserve 81590 centos  131u  IPv4 0xdeadbeef      0t0  TCP localhost:9042 (LISTEN)
yb-tserve 81593 centos  131u  IPv4 0xdeadbeef      0t0  TCP 127.0.0.2:9042 (LISTEN)
yb-tserve 81596 centos  131u  IPv4 0xdeadbeef      0t0  TCP 127.0.0.3:9042 (LISTEN)
```

If another process is using this port, you might need to stop that and restart the YB-TServer process. Otherwise, if no process is listening but the YB-TServer is running, check the value of the `--cql_proxy_bind_address` flag passed to the YB-TServer process.

## Can ycqlsh connect locally?

Once on the machine where YugabyteDB is running, use ycqlsh to connect to the local YugabyteDB instance, as follows:

```sh
ycqlsh <yb-local-address>
```

Depending on your configuration, you may need to install ycqlsh; otherwise, it is available in the YugabyteDB `bin` directory.

In the preceding command, `<yb-local-address>` is the address where the YugabyteDB YCQL server is listening (for example, as returned by `lsof`). For instance, it maps to `localhost` in the examples presented in this document (or, additionally, `127.0.0.2` and `127.0.0.3` for the yb-ctl case).

If ycqlsh can connect, the issue is likely a network issue with the original client not being able to access the node where YugabyteDB is running.

See also [Are YB-Master and YB-TServer endpoints accessible?](../../nodes/check-processes#are-the-yb-master-and-yb-tserver-endpoints-accessible)

---
title: Common error messages
linkTitle: Common error messages
headerTitle: Common error messages
description: How to understand and recover from common error messages
menu:
  preview:
    parent: troubleshoot-nodes
    weight: 50
type: docs
---

The following error messages are common to all YugabyteDB components.

## Skipping add replicas

When a new node has joined the cluster or an existing node has been removed, you may see error messages similar to the following:

```output
W1001 10:23:00.969424 22338 cluster_balance.cc:232] Skipping add replicas for 21d0a966e9c048978e35fad3cee31698:
Operation failed. Try again. Cannot add replicas. Currently have a total overreplication of 1, when max allowed is 1
```

This message is harmless and can be ignored. It means that the maximum number of concurrent tablets being remotely bootstrapped across the cluster by the YB-Master load balancer has reached its limit. This limit is configured in `--load_balancer_max_concurrent_tablet_remote_bootstraps` in [yb-master config](../../../reference/configuration/yb-master/#load-balancer-max-concurrent-tablet-remote-bootstraps).

## SST files limit exceeded

The following error is emitted when the number of SST files has exceeded its limit:

```output
Service unavailable (yb/tserver/tablet_service.cc:257): SST files limit exceeded 58 against (24, 48), score: 0.35422774182913203: 3.854s (tablet server delay 3.854s)
```

Usually, the client is running a high `INSERT`, `UPDATE`, `DELETE` workload and compactions are falling behind.

To determine why this error is happening, you can check the disk bandwidth, network bandwidth, and find out if enough CPU is available in the server.

The limits are controlled by the following YB-TServer configuration flags: `--sst_files_hard_limit=48` and `--sst_files_soft_limit=24`.

## Not able to perform operations using yb-admin after enabling encryption in transit

After configuring [encryption in transit](../../../secure/tls-encryption/) for a YugabyteDB cluster, you may encounter the following error when trying to use yb-admin:

```sh
./bin/yb-admin -master_addresses <master-addresses> list_all_masters
```

```output
Unable to establish connection to leader master at [MASTERIP1:7100,MASTERIP2:7100,MASTERIP3:7100].
Please verify the addresses.
Could not locate the leader master: GetLeaderMasterRpc(addrs: [MASTERIP1:7100, MASTERIP2:7100, MASTERIP3:7100], num_attempts: 338)
passed its deadline 79595.999s (passed: 60.038s): Network error (yb/util/net/socket.cc:535):
recvmsg got EOF from remote (system error 108)
Timed out (yb/rpc/rpc.cc:211):
Unable to establish connection to leader master at [MASTERIP1:7100,MASTERIP2:7100,MASTERIP3:7100].
Please verify the addresses.
Could not locate the leader master: GetLeaderMasterRpc(addrs: [MASTERIP1:7100, MASTERIP2:7100, MASTERIP3:7100]
```

To remedy the situation, you should pass the location of your certificates directory using the `-certs_dir_name` flag on the yb-admin command.

## ServerError: Server Error. Unknown keyspace/cf pair (system.peers_v2)

When connecting to the YCQL layer, you may encounter an error similar to the following:

```output.cql
ServerError: Server Error. Unknown keyspace/cf pair (system.peers_v2)
SELECT * FROM system.peers_v2;
^^^^^^
 (ql error -2)
```

The most likely reason is that you are not using one of the YugabyteDB forks of the Cassandra client drivers. The `system.peers_v2` table does not exist in YugabyteDB. To resolve this issue, you should check the [drivers page](../../../reference/drivers/ycql-client-drivers/) to find a driver for your client language.

## Poll stopped: Service unavailable

You may find messages similar to the following in your PostgreSQL logs:

```sh
I0208 00:01:18.247143 3895457 poller.cc:66] Poll stopped: Service unavailable (yb/rpc/scheduler.cc:80): Scheduler is shutting down (system error 108)
```

This is an informational message triggered by a process shutting down. It can be safely ignored.

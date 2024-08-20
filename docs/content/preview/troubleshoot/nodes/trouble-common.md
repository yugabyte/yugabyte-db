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

## Catalog Version Mismatch: A DDL occurred while processing this query

When executing queries in the YSQL layer, the query may fail with the following error:

```output
org.postgresql.util.PSQLException: ERROR: Catalog Version Mismatch: A DDL occurred while processing this query. Try Again
```

A DML query in YSQL may touch multiple servers, and each server has a Catalog Version which is used to track schema changes. When a DDL statement runs in the middle of the DML query, the Catalog Version is changed and the query has a mismatch, causing it to fail.

In these cases, the database aborts the query and returns a `40001` PostgreSQL error code. Errors with this code can be safely retried from the client side.

## Snapshot too old: When running ysql_dump

When running an `ysql_dump` command that takes too long to complete, you may encounter the following error:

```output
Snapshot too old: Snapshot too old. Read point: { physical: 1628678717824559 }, earliest read time allowed: { physical: 1628679675271006 }, delta (usec): 957446447: kSnapshotTooOld
```

When the command takes a long time to be processed, a compaction may have occurred and have deleted some rows at the snapshot the dump was started on. For large backups, it is recommended to use [distributed snapshots](../../../manage/backup-restore/snapshot-ysql/), which are more efficient and fast.

If you really need to use `ysql_dump`, you can increase the [`--timestamp_history_retention_interval_sec`](../../../reference/configuration/yb-tserver/#timestamp-history-retention-interval-sec) flag on the master to a higher value. The total time necessary for this command depends on the amount of metadata in your environment, thus you might need to tune this flag a couple of times. You can start by setting this flag to 3600 seconds and iterating from there. Note: Ideally, you don't want to leave this flag at a really high value, as that can have an adverse effect on the runtime of regular metadata queries (for example, DDLs, new connection establishment, metadata cache refreshes).

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

## ysqlsh: FATAL: password authentication failed for user "yugabyte" after fresh installation

You may encounter the following error when trying to connect to YSQL using the `ysqlsh` CLI after creating a fresh cluster:

```output
ysqlsh: FATAL:  password authentication failed for user "yugabyte"
```

By default, PostgreSQL listens on port `5432`. To avoid conflict, the YSQL port is set to `5433`. But because you can create multiple PostgreSQL clusters locally, each one takes the next port available, starting from `5433`, and thus conflicting with the YSQL port.

If you have created two PostgreSQL clusters before creating the YugabyteDB cluster, the `ysqlsh` shell is trying to connect to PostgreSQL running on port `5433` and fails to authenticate. To verify, you can run the following command to check which process is listening on port `5433`:

```sh
sudo lsof -i :5433
```

```output
COMMAND   PID     USER   FD   TYPE DEVICE SIZE/OFF NODE NAME
postgres 1263 postgres    7u  IPv4  35344      0t0  TCP localhost:postgresql (LISTEN)
```

You can shut down this PostgreSQL cluster or kill the process, and then restart YugabyteDB.

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

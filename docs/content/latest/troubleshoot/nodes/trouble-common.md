---
title: Common error messages
linkTitle: Common error messages
headerTitle: Understanding common error messages
description: How to understand and recover from common error messages
menu:
  latest:
    parent: troubleshoot-nodes
    weight: 50
isTocNested: true
showAsideToc: true
---

## Skipping add replicas

When a new node has joined the cluster or an existing node has been removed, you may see errors messages similar to the following:

```output
W1001 10:23:00.969424 22338 cluster_balance.cc:232] Skipping add replicas for 21d0a966e9c048978e35fad3cee31698: 
Operation failed. Try again. Cannot add replicas. Currently have a total overreplication of 1, when max allowed is 1
```

This message is harmless and can be ignored. It means that the maximum number of concurrent tablets being remotely bootstrapped across the  cluster by the YB-Master load balancer has reached its limit. This limit is configured in `--load_balancer_max_concurrent_tablet_remote_bootstraps` in [yb-master config](../../../reference/configuration/yb-master#load-balancer-max-concurrent-tablet-remote-bootstraps).

## SST files limit exceeded

The following error is emitted when the number of SST files has exceeded its limit:

```output
Service unavailable (yb/tserver/tablet_service.cc:257): SST files limit exceeded 58 against (24, 48), score: 0.35422774182913203: 3.854s (tablet server delay 3.854s)
```

Usually, the client is running a high INSERT/UPDATE/DELETE workload and compactions are falling behind. 

To determine why this error is happening, you can check the disk bandwidth, network bandwidth, and find out if enough CPU is available in the server.

The limits are controlled by the following YB-TServer configuration flags: `--sst_files_hard_limit=48` and `--sst_files_soft_limit=24`.

## Catalog Version Mismatch: A DDL occurred while processing this query. Try Again

When executing queries in the YSQL layer, the query may fail with the following error:

```output
org.postgresql.util.PSQLException: ERROR: Catalog Version Mismatch: A DDL occurred while processing this query. Try Again
```

A DML query in YSQL may touch multiple servers, and each server has a Catalog Version which is used to track schema changes. When a DDL statement runs in the middle of the DML query, the Catalog Version is changed and the query has a mismatch, causing it to fail.

In these cases, the database aborts the query and returns a `40001` PostgreSQL error code. Errors with this code can be safely retried from the client side. 

## Snapshot too old: When running ysql_dump

When running an `ysql_dump` command that takes too long to complete, you may get the following error:

```output
Snapshot too old: Snapshot too old. Read point: { physical: 1628678717824559 }, earliest read time allowed: { physical: 1628679675271006 }, delta (usec): 957446447: kSnapshotTooOld
```

When the command takes a long time to be processed, a compaction may have occurred and have deleted some rows at the snapshot the dump was started on. For large backups, we recommend using [distributed snapshots](../../../manage/backup-restore/snapshot-ysql), which are more efficient and faster.

If you really need to use ysql_dump, you can increase the [`--timestamp_history_retention_interval_sec`](../../../reference/configuration/yb-tserver#timestamp-history-retention-interval-sec) gflag in yb-tserver and retry.

## Not able to perform operations using yb-admin after enabling encryption at transit

After configuring [encryption at transit](../../../secure/tls-encryption) for yugabyte cluster, you may get the following error when trying to use `yb-admin`:

```sh
./bin/yb-admin -master_addresses <master-addresses> list_all_masters
```

```output
Unable to establish connection to leader master at [MASTERIP1:7100,MASTERIP2:7100,MASTERIP3:7100].
Please verify the addresses.\n\n: Could not locate the leader master: GetLeaderMasterRpc(addrs: [MASTERIP1:7100, MASTERIP2:7100, MASTERIP3:7100], num_attempts: 338)
passed its deadline 79595.999s (passed: 60.038s): Network error (yb/util/net/socket.cc:535):
recvmsg got EOF from remote (system error 108)\nTimed out (yb/rpc/rpc.cc:211):
Unable to establish connection to leader master at [MASTERIP1:7100,MASTERIP2:7100,MASTERIP3:7100].
Please verify the addresses.\n\n: Could not locate the leader master: GetLeaderMasterRpc(addrs: [MASTERIP1:7100, MASTERIP2:7100, MASTERIP3:7100]
```

Pass the location of your certificates directory via `--certs_dir_name` on the `yb-admin` command. Then it will function as expected.

## ysqlsh: FATAL: password authentication failed for user "yugabyte" after fresh installation

Sometimes users get the following error when trying to connect to YSQL using the `ysqlsh` CLI after creating a fresh cluster:

```output
ysqlsh: FATAL:  password authentication failed for user "yugabyte"
```

By default, PostgreSQL listens on port `5432`. To not conflict with it, we've set the YSQL port to `5433`. But users have the ability to create multiple PostgreSQL clusters locally. Each one takes the next port available, starting from `5433`, conflicting with the YSQL port. 

If you've created 2 PostgreSQL clusters before creating the YugabyteDB cluster, the `ysqlsh` shell is trying to connect to PostgreSQL running on port `5433` and failing to authenticate. To verify in this case, you can look which process is listening on port `5433`:

```sh
sudo lsof -i :5433
```

```output
COMMAND   PID     USER   FD   TYPE DEVICE SIZE/OFF NODE NAME
postgres 1263 postgres    7u  IPv4  35344      0t0  TCP localhost:postgresql (LISTEN)
```

Shut down this PostgreSQL cluster, or kill the process, then restart YugabyteDB.

## ServerError: Server Error. Unknown keyspace/cf pair (system.peers_v2)

When connecting to the YCQL layer, you may get an error similar to the following:

```output.cql
ServerError: Server Error. Unknown keyspace/cf pair (system.peers_v2)
SELECT * FROM system.peers_v2;
^^^^^^
 (ql error -2)
```

The reason is probably that you're not using one of our forks of the Cassandra client drivers. The `system.peers_v2` table doesn't exist in YugabyteDB. Check the [drivers page](../../../reference/drivers/ycql-client-drivers) to find a driver for your client language.

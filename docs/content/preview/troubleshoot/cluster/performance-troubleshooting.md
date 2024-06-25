---
title: Troubleshoot performance issues
headerTitle: Performance issues
linkTitle: Performance issues
description: Investigate and troubleshoot performance issues in YugabyteDB clusters
aliases:
  - /preview/benchmark/performance-troubleshooting/
menu:
  preview:
    identifier: performance-troubleshooting
    parent: troubleshoot-cluster
    weight: 868
type: docs
---

There is a number of steps you can take to investigate and troubleshoot the performance of your YugabyteDB clusters.

## Files on a YugabyteDB cluster

Learning about the default locations of files on a YugabyteDB cluster can help with troubleshooting the cluster performance issues.

Note that the following locations are applicable to clusters installed via YugabyteDB Anywhere.

### YugabyteDB software and binary files

The software packages are symlinked at `/home/yugabyte/{master|tserver}`.

Note that YB-Master and YB-TServer may be different versions of the software (for example, this could be a result of rolling software upgrades).

For example, to learn file locations on YB-Master, execute the following command:

```sh
ls -lrt /home/yugabyte/master
```

Expect an output similar to the following:

```output
total 4
lrwxrwxrwx. 1 yugabyte yugabyte   27 Jan 15 19:27 logs -> /mnt/d0/yb-data/master/logs
lrwxrwxrwx. 1 yugabyte yugabyte   66 Jan 15 19:28 bin -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/bin
lrwxrwxrwx. 1 yugabyte yugabyte   66 Jan 15 19:28 lib -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/lib
lrwxrwxrwx. 1 yugabyte yugabyte   72 Jan 15 19:28 linuxbrew -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/linuxbrew
lrwxrwxrwx. 1 yugabyte yugabyte   85 Jan 15 19:28 linuxbrew-xxxxxxxxxxxx -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/linuxbrew-xxxxxxxxxxxx
lrwxrwxrwx. 1 yugabyte yugabyte   71 Jan 15 19:28 postgres -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/postgres
lrwxrwxrwx. 1 yugabyte yugabyte   68 Jan 15 19:28 pylib -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/pylib
lrwxrwxrwx. 1 yugabyte yugabyte   68 Jan 15 19:28 share -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/share
lrwxrwxrwx. 1 yugabyte yugabyte   68 Jan 15 19:28 tools -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/tools
lrwxrwxrwx. 1 yugabyte yugabyte   65 Jan 15 19:28 ui -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/ui
lrwxrwxrwx. 1 yugabyte yugabyte   84 Jan 15 19:28 version_metadata.json -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/version_metadata.json
lrwxrwxrwx. 1 yugabyte yugabyte   66 Jan 15 19:28 www -> /home/yugabyte/yb-software/yugabyte-2.5.1.0-b187-centos-x86_64/www
-rw-rw-r--. 1 yugabyte yugabyte    0 Jan 15 19:29 master.out
-rw-rw-r--. 1 yugabyte yugabyte 2200 Jan 15 20:18 master.err
drwxr-xr-x. 2 yugabyte yugabyte   25 Jan 15 20:18 conf
```

### Configuration files

Configuration files for the YB-Master and YB-TServer are available in `/home/yugabyte/{master|tserver}/conf/server.conf`.

### Transaction logs

If YugabyteDB was run with `--fs_data_dirs=/mnt/d0,/mnt/d1`, you can find transaction logs (also known as write-ahead logs (WALs)) at `/mnt/d*/yb-data/{master|tserver}/wals`.

To print the contents of the WAL, use the `log-dump` utility, as follows:

```sh
./home/yugabyte/tserver/bin/log-dump /mnt/d0/yb-data/tserver/wals/table-e85a116bc557403e82f57037e7b13879/tablet-05bef5ed6fb74cabb420b648b6f850e3/

# use -print_entries=pb to print the entire contents of each record
```

### Database files for tablets

The database (also known as SSTable) files are located at `/mnt/d*/yb-data/{master|tserver}/data`.

You can print the contents of the SSTable files as follows:

```sh
./home/yugabyte/tserver/bin/ldb dump --compression_type=snappy --db=/mnt/d0/yb-data/tserver/data/table-e85a116bc557403e82f57037e7b13879/tablet-05bef5ed6fb74cabb420b648b6f850e3/
```

### Debug logs

Debug logs are output to `/home/yugabyte/{master|tserver}/logs`.

### Standard output and standard error

`stderr` and `stdout` of YB-Master and YB-TServer processes are output to `/home/yugabyte/tserver/tserver.{err|out}`.

## Slow response logs

Slow responses take more than 75% of the configured RPC timeout. By default, these responses are logged at a `WARNING` level in the following format, with a breakdown of time in various stages:

```output
W0325 06:47:13.032176 116514816 inbound_call.cc:204] Call yb.consensus.ConsensusService.UpdateConsensus from 127.0.0.1:61050 (request call id 22856) took 2644ms (client timeout 1000).
W0325 06:47:13.033341 116514816 inbound_call.cc:208] Trace:
0325 06:47:10.388015 (+     0us) service_pool.cc:109] Inserting onto call queue
0325 06:47:10.394859 (+  6844us) service_pool.cc:170] Handling call
0325 06:47:10.450697 (+ 55838us) raft_consensus.cc:1026] Updating replica for 0 ops
0325 06:47:13.032064 (+2581367us) raft_consensus.cc:1242] Filling consensus response to leader.
0325 06:47:13.032106 (+    42us) spinlock_profiling.cc:233] Waited 2.58 s on lock 0x108dc3d40. stack: 0000000103d63b0c 0000000103d639fc 00000001040ac908 0000000102f698ec 0000000102f698a4 0000000102f93039 0000000102f7e124 0000000102fdcf7b 0000000102fd90c9 00000001\
02504396 00000001032959f5 0000000103470473 0000000103473491 00000001034733ef 000000010347338b 000000010347314c
0325 06:47:13.032168 (+    62us) inbound_call.cc:125] Queueing success response
```

## yb-ts-cli

You can run various tablet-related commands with `yb-ts-cli` by pointing at the YB-Master, as follows:

```sh
./yb-ts-cli list_tablets --server_address=localhost:9000
./yb-ts-cli dump_tablet --server_address=localhost:9000 e1bc59288ee849ab850ae0a40bd88649
```

## yb-admin

You can run various commands with `yb-admin`. You need to specify the full set of YB-Master ports `{ip:ports}` with `-master_addresses`, as follows:

```sh
# Get all tables
./yb-admin -master_addresses 127.0.0.1:7000,127.0.0.2:7000,127.0.0.3:7000 list_tables

# Get all tablets for a specific table
./yb-admin -master_addresses 127.0.0.1:7000,127.0.0.2:7000,127.0.0.3:7000 list_tablets yb_load_test

# List the tablet servers for each tablet
./yb-admin -master_addresses 127.0.0.1:7000,127.0.0.2:7000,127.0.0.3:7000 list_tablet_servers $(./yb-admin -master_addresses 127.0.0.1:7000,127.0.0.2:7000,127.0.0.3:7000 list_tablets yb_load_test)

# List all tablet servers
./yb-admin -master_addresses 127.0.0.1:7000,127.0.0.2:7000,127.0.0.3:7000 list_all_tablet_servers

# List all masters
./yb-admin -master_addresses 127.0.0.1:7000,127.0.0.2:7000,127.0.0.3:7000 list_all_masters

# Output master state to console
./yb-admin -master_addresses 127.0.0.1:7000,127.0.0.2:7000,127.0.0.3:7000 dump_masters_state
```

## Real-time metrics

You can view metrics of various YugabyteDB processes at a particular node (for example, 127.0.0.1) at the following ports:

| Process | Address |
| :------ | :------ |
| Master | 127.0.0.1:7000 |
| TServer | 127.0.0.1:9000 |
| YCQL | 127.0.0.1:12000 |
| YSQL | 127.0.0.1:13000 |

For each process, you can see the following types of metrics:

| Description | Endpoint |
| :---------- | :------- |
| Per-Tablet, JSON Metrics | /metrics |
| Per-Table, Prometheus Metrics | /prometheus-metrics |

## RPC tracing

To enable tracing, you can set the `enable_tracing` flag, as follows:

```sh
./yb-ts-cli --server_address=localhost:9100 set_flag enable_tracing 1
```

To enable tracing for all RPCs (not just the slow ones) including the `enable_tracing` flag, you may also set the `rpc_dump_all_traces` gflag, as follows:

```sh
./yb-ts-cli --server_address=localhost:9100 set_flag rpc_dump_all_traces 1
```

## Dynamic settings for gflags

Although setting string gflags dynamically is not recommended as it is not thread-safe, the `yb-ts-cli` utility allows you to do that.

Before attempting to set gflags, you need to identify the server using its Remote Procedure Call (RPC) port, as opposed to the HTTP port.

For example, you can increase the verbose logging level to 2 by executing the following command:

```sh
./yb-ts-cli --server_address=localhost:9100 set_flag v 2
```

## Proto file contents

To dump the contents of a file containing a proto (such as a file in the consensus-meta or tablet-meta directory), use the `yb-pbc-dump` utility, as follows:

```sh
./yb-pbc-dump /mnt/d0/yb-data/tserver/consensus-meta/dd57975ef2f2440497b5d96fc32146d3
./yb-pbc-dump /mnt/d0/yb-data/tserver/tablet-meta/bfb3f18736514eeb841b0307a066e66c
```

On macOS, the environment variable `DYLD_FALLBACK_LIBRARY_PATH` needs to be set for `pbc-dump` to work. To set this variable, add the following to `~/.bash_profile`:

```sh
export DYLD_FALLBACK_LIBRARY_PATH=~/code/yugabyte/build/latest/rocksdb-build
```

---
title: yb-tserver configuration reference
headerTitle: yb-tserver
linkTitle: yb-tserver
description: YugabyteDB Tablet Server (yb-tserver) binary and configuration flags to store and manage data for client applications.
menu:
  preview:
    identifier: yb-tserver
    parent: configuration
    weight: 2440
type: docs
---

The YugabyteDB Tablet Server ([YB-TServer](../../../architecture/yb-tserver/)) is a critical component responsible for managing data storage, processing client requests, and handling replication within a YugabyteDB cluster. It ensures data consistency, fault tolerance, and scalability by storing and serving data as tablets—sharded units of storage distributed across multiple nodes. Proper configuration of the YB-TServer is important to optimize performance, manage system resources effectively, help establish secure communication, and provide high availability (HA).

This page provides detailed information about various configuration flags available for `yb-tserver`. Each flag allows administrators and developers to fine-tune the server's behavior according to their deployment requirements and performance objectives.

Use the yb-tserver binary and its flags to configure the YB-TServer server. The `yb-tserver` executable file is located in the `bin` directory of YugabyteDB home.

**Syntax**

```sh
yb-tserver [ flags ]
```

**Example**

```sh
./bin/yb-tserver \
--tserver_master_addrs 172.151.17.130:7100,172.151.17.220:7100,172.151.17.140:7100 \
--rpc_bind_addresses 172.151.17.130 \
--enable_ysql \
--fs_data_dirs "/home/centos/disk1,/home/centos/disk2" &
```

**Online help**

To display the online help, run `yb-tserver --help` from the YugabyteDB home directory:

```sh
./bin/yb-tserver --help
```

Use `--helpon` to displays help on modules named by the specified flag value.

This page categorizes configuration flags into the following sections, making it easier to navigate:

| Category                     | Description |
|------------------------------|-------------|
| [General Configuration](#general-configuration)        | Basic server setup including overall system settings, logging, and web interface configurations. |
| [Networking](#networking)                   | Flags that control network interfaces, RPC endpoints, DNS caching, and geo-distribution settings. |
| [Storage & Data Management](#storage--data-management)    | Parameters for managing data directories, WAL configurations, sharding, CDC, and TTL-based file expiration. |
| [Performance Tuning](#performance-tuning)           | Options for resource allocation, memory management, compaction settings, and overall performance optimizations. |
| [Security](#security)                     | Settings for encryption, SSL/TLS, and authentication to secure both node-to-node and client-server communications. |

## General Configuration

##### --version

Shows version and build info, then exits.

##### --flagfile

Specifies the file to load the configuration flags from. The configuration flags must be in the same format as supported by the command line flags. (Default: None - stable)

##### --fs_data_dirs

Specifies a comma-separated list of mount directories, where yb-tserver will add a `yb-data/tserver` data directory, `tserver.err`, `tserver.out`, and `pg_data` directory. Changing the value of this flag after the cluster has already been created is not supported. This argument must be specified.  (Default: None - stable)

##### --fs_wal_dirs

Specifies a comma-separated list of directories, where yb-tserver will store write-ahead (WAL) logs. This can be the same as one of the directories listed in `--fs_data_dirs`, but not a subdirectory of a data directory. This is an optional argument. (Default: The same value as `--fs_data_dirs`)

##### --max_clock_skew_usec

Specifies the expected maximum clock skew, in microseconds (µs), between any two nodes in your deployment. (Default: `500000` (500,000 µs = 500ms))

##### --tablet_server_svc_queue_length

Specifies the RPC queue size for the tablet server to serve reads and writes from applications. (Default: `5000`)

##### --webserver_interface

The address to bind for the web server user interface. (Default: `0.0.0.0` (`127.0.0.1`))

## Networking

##### --tserver_master_addrs

Comma separated RPC addresses of the YB-Masters which the tablet server should connect to. The CQL proxy reads this flag as well to determine the new set of masters. The number of comma-separated values should match the total number of YB-Master servers (or the replication factor). This argument must be specified. (Default: 127.0.0.1:7100 - stable) 

##### --rpc_bind_addresses

Specifies the comma-separated list of the network interface addresses to which to bind for RPC connections. (Default: Private IP address of the host on which the server is running, as defined in `/home/yugabyte/tserver/conf/server.conf`) *

##### --server_broadcast_addresses

Specifies the public IP or DNS hostname of the server (with an optional port). This value is used by servers to communicate with one another, depending on the connection policy parameter.  (Default: None)

##### --dns_cache_expiration_ms

Specifies the duration, in milliseconds, until a cached DNS resolution expires. When hostnames are used instead of IP addresses, a DNS resolver must be queried to match hostnames to IP addresses. By using a local DNS cache to temporarily store DNS lookups, DNS queries can be resolved quicker and additional queries can be avoided, thereby reducing latency, improving load times, and reducing bandwidth and CPU consumption. (Default: `60000` (1 minute)). 

If you change this value from the default, be sure to add the identical value to all YB-Master and YB-TServer configurations.

##### --use_private_ip

Specifies the policy that determines when to use private IP addresses for inter-node communication. Possible values are `never`, `zone`, `cloud`, and `region`. (Default: `never`)*

##### --enable_stream_compression

Controls whether YugabyteDB uses RPC streamm compression. (Default: `true`)

##### --stream_compression_algo

Specifies which RPC compression algorithm to use. Requires `enable_stream_compression` to be set to true. Valid values are:

0: No compression (default value)
1: Gzip
2: Snappy
3: LZ4

In most cases, LZ4 (`--stream_compression_algo=3`) offers the best compromise of compression performance versus CPU overhead. However, the default is set to 0, to avoid latency penalty on workloads.

## Storage & Data Management

##### --yb_num_shards_per_tserver

The default number of shards (tablets) per YB-TServer for each YCQL table when a user table is created. If the value is -1, the system automatically determines an appropriate value based on the number of CPU cores; it is determined to 1 if enable_automatic_tablet_splitting is set to true. (Default: -1 - runtime) *

##### --ysql_num_shards_per_tserver

The default number of shards (tablets) per YB-TServer for each YSQL table when a user table is created. If the value is -1, the system automatically determines an appropriate value based on the number of CPU cores; it is determined to 1 if enable_automatic_tablet_splitting is set to true. (Default: -1 - runtime) *

##### --yb_enable_cdc_consistent_snapshot_streams

Enable support for creating streams for transactional CDC. Support for creating a stream for Transactional CDC is currently in [Tech Preview](/preview/releases/versioning/#feature-maturity). (Default: `false`) * 

<!-- default in yb all server is different from tserver page -->

##### --cdc_state_checkpoint_update_interval_ms

Rate at which CDC state's checkpoint is updated. (Default: 15000 - runtime)

##### --cdc_ybclient_reactor_threads

The number of reactor threads to be used for processing `ybclient` requests for CDC. Increase to improve throughput on large tablet setups. (Default: `50`)

<!-- all tserver page mention it as deprecreated -->

##### --cdc_max_stream_intent_records

Maximum number of intent records allowed in a single CDC batch.

Default: `1000`

<!-- default is different in all tserver flags vs tserver page -->

## Performance Tuning

##### --use_memory_defaults_optimized_for_ysql

If true, the recommended defaults for the memory usage settings take into account the amount of RAM and cores available and are optimized for using YSQL. If false, the recommended defaults will be the old defaults, which are more suitable for YCQL but do not take into account the amount of RAM and cores available. (Default: false) *

##### --memory_limit_hard_bytes

Maximum amount of memory this process should use in bytes, that is, its hard memory limit.  A value of `0` specifies to instead use a percentage of the total system memory; see [--default_memory_limit_to_ram_ratio](#--default-memory-limit-to-ram-ratio) for the percentage used.  A value of `-1` disables all memory limiting. (Default: 0 - stable)

##### --db_block_cache_size_bytes

Size of the shared RocksDB block cache (in bytes).  A value of `-1` specifies to instead use a percentage of this processes' hard memory limit; see [--db_block_cache_size_percentage](#--db-block-cache-size-percentage) for the percentage used.  A value of `-2` disables the block cache. (Default: -1)

##### --evict_failed_followers

Whether to evict followers from the Raft config that have fallen too far behind the leader's log to catch up normally or have been unreachable by the leader for longer than [`--follower_unavailable_considered_failed_sec`](#--follower_unavailable_considered_failed_sec) (Default: true - advanced)

##### --follower_unavailable_considered_failed_sec

The duration, in seconds, after which a follower is considered to be failed because the leader has not received a heartbeat. The follower is then evicted from the configuration and the data is re-replicated elsewhere. (Default: `900` (15 minutes)) * 

## Security

##### --certs_dir

Directory that contains certificate authority, private key and certificates for this server. By default 'certs' subdir in data folder is used. (Default: None , Uses `<data drive>/yb-data/tserver/data/certs` )

##### --certs_for_client_dir

Directory that contains certificate authority, private key and certificates for this server that should be used for client to server communications. When empty, the same dir as for server to server communications is used. (Default: None (Use the same directory as certs_dir.))

##### --allow_insecure_connections

Allow insecure connections. Set to `false` to prevent any process with unencrypted communication from joining a cluster. *
(Default: `true`)

##### --dump_certificate_entries

Adds certificate entries, including IP addresses and hostnames, to log for handshake error messages. Enable this flag to debug certificate issues. (Default: `false`)

##### --use_client_to_server_encryption

Use client-to-server (client-to-node) encryption to protect data in transit between YugabyteDB servers and clients, tools, and APIs. (Default: `false`)

##### --use_node_to_node_encryption

Enable server-server (node-to-node) encryption between YugabyteDB YB-Master and YB-TServer servers in a cluster or universe.  *

##### --cipher_list

Define the list of available ciphers (TLSv1.2 and below). (Default: None) *
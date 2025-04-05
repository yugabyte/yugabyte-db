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

| Category | Description |
|-----------|---------|
| General & Networking | Configuration flags for server addresses, networking, and general cluster settings. |
| Storage & Data Management | Flags related to data directories, WAL configuration, and data retention policies. |
| Memory & Performance | Controls resource allocation, memory management, and performance optimizations. |
| Security & Authentication | Manages encryption, SSL/TLS security settings, and authentication methods for secure communication. |
| Advanced Features & Debugging | Provides access to advanced features, debugging tools, and experimental settings. |

### General & Networking

| Flag Name                   | Type    | Description                                                            | Default              |
|-----------------------------|---------|------------------------------------------------------------------------|----------------------|
| `--flagfile`                | String  | Specifies file to load configuration flags from.                       | `""`                 |
| `--rpc_bind_addresses`      | String  | Comma-separated IPs for RPC server binding.                            | Host private IP      |
| `--server_broadcast_addresses` | String  | Public IP or DNS for inter-node communication.                         | `""`                 |
| `--dns_cache_expiration_ms` | Integer | DNS cache expiration in milliseconds.                                  | `60000`              |
| `--max_clock_skew_usec`     | Integer | Max expected clock skew between nodes, in microseconds.                | `500000`             |

### Storage & Data Management

| Flag Name                | Type    | Description                                                             | Default              |
|--------------------------|---------|-------------------------------------------------------------------------|----------------------|
| `--fs_data_dirs`         | String  | Comma-separated list of mount paths for YB-TServer data.                | Required             |
| `--fs_wal_dirs`          | String  | WAL log storage directory.                                              | Same as `fs_data_dirs` |
| `--log_segment_size_mb`  | Integer | Size of each WAL log segment in MB.                                     | `64`                 |
| `--enable_automatic_tablet_splitting` | Boolean | Enables automatic tablet splitting.                              | `true`               |
| `--yb_num_shards_per_tserver` | Integer | Default number of YCQL shards per tserver.                             | `-1` (auto)          |


### Memory & Performance

| Flag Name                          | Type    | Description                                                             | Default              |
|------------------------------------|---------|-------------------------------------------------------------------------|----------------------|
| `--memory_limit_hard_bytes`        | Integer | Hard memory limit for the process in bytes.                             | `0`                  |
| `--default_memory_limit_to_ram_ratio` | Float  | RAM percentage used if hard byte limit is `0`.                          | `0.85`               |
| `--db_block_cache_size_percentage` | Integer | Percentage of memory for RocksDB block cache.                          | `50`                 |
| `--tablet_overhead_size_percentage`| Integer | Percent of memory reserved for tablet overhead.                         | `0`                  |
| `--use_memory_defaults_optimized_for_ysql` | Boolean | Optimizes memory allocation for YSQL-heavy workloads.         | `false`              |

### Security & Authentication

| Flag Name                       | Type    | Description                                                            | Default              |
|---------------------------------|---------|------------------------------------------------------------------------|----------------------|
| `--certs_dir`                   | String  | Directory containing SSL certs for server communication.               | `""`                 |
| `--use_node_to_node_encryption`| Boolean | Enable encrypted communication between nodes.                          | `false`              |
| `--use_client_to_server_encryption` | Boolean | Enable TLS encryption between client and server.                  | `false`              |
| `--allow_insecure_connections` | Boolean | Allow non-TLS connections (only when encryption is disabled).          | `true`               |
| `--ssl_protocols`              | String  | Allowed list of TLS protocols for RPC communication.                   | `""`                 |

### Advanced Features & Debugging

| Flag Name                          | Type    | Description                                                            | Default              |
|------------------------------------|---------|------------------------------------------------------------------------|----------------------|
| `--log_dir`                        | String  | Directory for writing logs.                                            | Same as `fs_data_dirs` |
| `--raft_heartbeat_interval_ms`     | Integer | Time between Raft heartbeat messages.                                  | `500`                |
| `--enable_stream_compression`     | Boolean | Enables compression of RPC messages.                                   | `true`               |
| `--stream_compression_algo`       | Integer | Algorithm used for compression (e.g., LZ4 = 3).                         | `0` (None)           |
| `--cdc_enable`                    | Boolean | Enable Change Data Capture (CDC) functionality.                        | `false`              |

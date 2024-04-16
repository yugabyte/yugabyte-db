---
title: yb-ts-cli - command line tool for advanced yb-tserver operations
headerTitle: yb-ts-cli
linkTitle: yb-ts-cli
description: Use the yb-ts-cli command line utility to perform advanced YB-TServer operations.
menu:
  preview:
    identifier: yb-ts-cli
    parent: admin
    weight: 50
type: docs
---

`yb-ts-cli` is a command line tool that can be used to perform an operation on a particular tablet server (`yb-tserver`). Some of the commands perform operations similar to [`yb-admin` commands](../yb-admin/). The `yb-admin` commands focus on cluster administration, the `yb-ts-cli` commands apply to specific YB-TServer nodes.

`yb-ts-cli` is a binary file installed with YugabyteDB and is located in the `bin` directory of the YugabyteDB home directory.

## Syntax

```sh
yb-ts-cli [ --server_address=<host>:<port> ] <command> <flags>
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.
* *command*: The operation to be performed. See [Commands](#commands).
* *flags*: The flags to be applied to the command. See [Flags](#flags).

### Online help

To display the available online help, run `yb-ts-cli` without any commands or flags at the YugabyteDB home directory.

```sh
./bin/yb-ts-cli
```

## Commands

The following commands are available:

* [are_tablets_running](#are-tablets-running)
* [is_server_ready](#is-server-ready)
* [clear_server_metacache](#clear-server-metacache)
* [compact_all_tablets](#compact-all-tablets)
* [compact_tablet](#compact-tablet)
* [count_intents](#count-intents)
* [current_hybrid_time](#current-hybrid-time)
* [delete_tablet](#delete-tablet)
* [dump_tablet](#dump-tablet)
* [flush_all_tablets](#flush-all-tablets)
* [flush_tablet](#flush-tablet)
* [list_tablets](#list-tablets)
* [reload_certificates](#reload-certificates)
* [remote_bootstrap](#remote-bootstrap)
* [set_flag](#set-flag)
* [status](#status)
* [refresh_flags](#refresh-flags)

##### are_tablets_running

If all tablets are running, returns "All tablets are running".

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] are_tablets_running
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.

##### is_server_ready

Prints the number of tablets that have not yet bootstrapped.
If all tablets have bootstrapped, returns "Tablet server is ready".

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] is_server_ready
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.

##### clear_server_metacache

Clears all metacaches that are stored on a specified server. Works on both YB-Master (port 9100) and YB-TServer (port 7100) processes. Tablet servers and masters use MetaCaches to cache information about which tablet server hosts which tablet. Because these caches could become stale in some cases, you may want to use this command to clear the MetaCaches on a particular tablet server or master.

**Syntax**
```sh
yb-ts-cli [ --server_address=<host>:<port> ] clear_server_metacache
```

* *host*:*port*: The *host* and *port* of the tablet/master server. Default is `localhost:9100`.

##### compact_all_tablets

Compact all tablets on the tablet server.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] compact_all_tablets
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.

##### compact_tablet

Compact the specified tablet on the tablet server.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] compact_tablet <tablet_id>
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.
* *tablet_id*: The identifier of the tablet to compact.

##### count_intents

Print the count of uncommitted intents (or [provisional records](../../architecture/transactions/distributed-txns/#provisional-records)). Helpful for debugging transactional workloads.

**Syntax**

```sh
yb-ts-cli  [ --server_address=<host>:<port> ] count_intents
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.

##### current_hybrid_time

Prints the value of the current [hybrid time](../../architecture/transactions/transactions-overview/#mvcc-using-hybrid-time).

**Syntax**

```sh
yb-ts-cli  [ --server_address=<host>:<port> ] current_hybrid_time
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.

##### delete_tablet

Deletes the tablet with the specified tablet ID (`tablet_id`) and reason.

**Syntax**

```sh
yb-ts-cli  [ --server_address=<host>:<port> ] delete_tablet <tablet_id> "<reason-string>"
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.
* *tablet_id*: The identifier (ID) for the tablet.
* *reason-string*: Text string providing information on why the tablet was deleted.

##### dump_tablet

Dump, or export, the specified tablet ID (`tablet_id`).

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] dump_tablet <tablet_id>
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.
* *tablet_id*: The identifier (ID) for the tablet.

##### flush_all_tablets

Flush all tablets on the tablet server.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] flush_all_tablets
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.

##### flush_tablet

Flush the specified tablet on the tablet server.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] flush_tablet <tablet_id>
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.
* *tablet_id*: The identifier of the tablet to compact.

##### list_tablets

Lists the tablets on the specified tablet server, displaying the following properties: column name, tablet ID, state, table name, shard, and schema.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] list_tablets
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.

##### reload_certificates

Trigger a reload of TLS certificates and private keys from disk on the specified (master or tablet) server.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] reload_certificates
```

* *host*:*port*: The *host* and *port* of the master or tablet server. Default is `localhost:9100`.

##### remote_bootstrap

Trigger a remote bootstrap of a tablet from another tablet server to the specified tablet server.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] remote_bootstrap <source_host> <tablet_id>
```

* *host*:*port*: The *host* and *port* of the tablet server running the remote bootstrap. Default is `localhost:9100`.
* *source_host*: The *host* or *host* and *port* of the tablet server to bootstrap from.
* *tablet_id*: The identifier of the tablet to trigger a remote bootstrap for.

See [Manual remote bootstrap of failed peer](../../troubleshoot/cluster/replace_failed_peers/) for example usage.

##### set_flag

Sets the specified configuration flag for the tablet server.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] set_flag [ --force ] <flag> <value>
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.
* `--force`: Flag to allow a change to a flag that is not explicitly marked as runtime-settable. Note that the change may be ignored on the server or may cause the server to crash, if unsafe values are provided. See [--force](#force).
* *flag*: The `yb-tserver` configuration flag (without the `--` prefix) to be set. See [`yb-tserver`](../../reference/configuration/yb-tserver/)
* *value*: The value to be applied.

{{< note title="Important" >}}

The `set_flag` command changes the in-memory value of the specified flag, atomically, for a running server and can alter its behavior.  **The change does NOT persist across restarts.**

In practice, there are some flags that are runtime safe to change (runtime-settable) and some that are not. For example, the bind address of the server cannot be changed at runtime, because the server binds just once at startup. While most of the flags are probably runtime-settable, you need to review the flags and note in the configuration pages which flags are not runtime-settable. (See GitHub issue [#3534](https://github.com/yugabyte/yugabyte-db/issues/3534)).

One typical operational flow is that you can use this to modify runtime flags in memory and then out of band also modify the configuration file that the server uses to start. This allows for flags to be changed on running servers, without executing a restart of the server.

{{< /note >}}

##### status

Prints the status of the tablet server, including information on the node instance, bound RPC addresses, bound HTTP addresses, and version information.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] status
```

* *host*:*port*: The *host* and *port* of the tablet server. Default is `localhost:9100`.

For an example, see [Return the status of a tablet server](#return-the-status-of-a-tablet-server)

##### refresh_flags

Refresh flags that are loaded from the configuration file. Works on both YB-Master (port 9100) and YB-TServer (port 7100) process. No parameters needed.

Each process needs to have the following command issued, for example, issuing the command on one YB-TServer won't update the flags on the other YB-TServers.

**Syntax**

```sh
yb-ts-cli [ --server_address=<host>:<port> ] refresh_flags
```

* *host*:*port*: The *host* and *port* of the YB-Master or YB-TServer. Default is `localhost:9100`.

## Flags

The following flags can be used, when specified, with the commands above.

##### --force

Use this flag with the [`set_flag`](#set-flag) command to allow a change to a flag that is not explicitly marked as runtime-settable. Note that the change may be ignored on the server or may cause the server to crash, if unsafe values are provided.

Default: `false`

##### --server-address

The address (*host* and *port*) of the tablet server to run against.

Default: `localhost:9100`

##### --timeout_ms

The duration, in milliseconds (ms), before the RPC request times out.

Default: `60000` (1000 ms = 1 sec)

##### --certs_dir_name

To connect to a cluster with TLS enabled, you must include the `--certs_dir_name` flag with the directory location where the root certificate is located.

Default: `""`

## Examples

### Return the status of a tablet server

```sh
./bin/yb-ts-cli --server_address=127.0.0.1 --certs_dir_name="/path/to/dir/name" status
```

```output
node_instance {
  permanent_uuid: "237678d61086489991080bdfc68a28db"
  instance_seqno: 1579278624770505
}
bound_rpc_addresses {
  host: "127.0.0.1"
  port: 9100
}
bound_http_addresses {
  host: "127.0.0.1"
  port: 9000
}
version_info {
  git_hash: "83610e77c7659c7587bc0c8aea76db47ff8e2df1"
  build_hostname: "yb-macmini-6.dev.yugabyte.com"
  build_timestamp: "06 Jan 2020 17:47:22 PST"
  build_username: "jenkins"
  build_clean_repo: true
  build_id: "743"
  build_type: "RELEASE"
  version_number: "2.0.10.0"
  build_number: "4"
}
```

### Display the current hybrid time

```sh
./bin/yb-ts-cli  --server_address=yb-tserver-1:9100 current_hybrid_time
```

```output
6470519323472437248
```

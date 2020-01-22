---
title: yb-ts-cli
linkTitle: yb-ts-cli
description: yb-ts-cli
menu:
  latest:
    identifier: yb-ts-tool
    parent: admin
    weight: 2466
isTocNested: 3
showAsideToc: true
---

## Syntax

```sh
./yb-ts-cli [--server_address=<addr>] <command> <options>
```
- *command*: asdfad
- *options*: asfads

## Command line help



## Commands

### list_tablets

### are_tablets_running

If all tablets are running, returns "All tablets are running".

### set_flag

Set the specified option (flag).

### dump_tablet.

Dump the tablet with the specified tablet ID (`tablet_id`).

### delete_tablet

Delete the tablet with the specified tablet ID (`tablet_id`).

### current_hybrid_time



### status

Returns the status of the tablet servers.


### count_intents

## Options

##### -force

If `true`, allows the `set_flag` command to set an option which is not explicitly marked as runtime-settable. The change may be ignored on the server or may cause the server to crash.

Default: `false`

##### -server-address

The address of the YB-TServer to run against.

Default: `localhost`

##### -timeout_ms

The duration, in milliseconds (ms), before RPC request fails.

Default: `60000` (1000 ms = 1 sec)

## Examples

### Return the status of the specified tablet server

```sh
$ ./bin/yb-ts-cli status -server_address=127.0.0.1
```

```json
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
$ ./bin/yb-ts-cli current_hybrid_time
```

```
6470519323472437248
```

### 
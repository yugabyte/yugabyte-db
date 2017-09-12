---
date: 2016-03-09T00:11:02+01:00
title: yb-tserver Reference
weight: 15
---

`yb-tserver`, located in the bin directory of YugaByte home, is the [YB-TServer](/architecture/concepts/#yb-tserver) binrary.

## Help command

Use the **-\-help** option to see all the commands supported.

```sh
$ ./bin/yb-tserver --help
```

## Supported config flags

Flag | Type | Default | Description 
-------------------|-------------|-------------|----------------------------------------
`--fs_data_dirs` | string | none | Comma-separated list of directories where the Master will place its data blocks. 
`--fs_wal_dirs`| string | If not specified, then same as `--fs_data_dirs` | The directory where the Master will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory. 
`--log_dir`| string | none | The directory to store Master log files.  
`--webserver_port`| string |  `8051` |
`--rpc_bind_addresses`| string | `0.0.0.0` | Comma-separated list of addresses to bind to for RPC connections.
`--webserver_doc_root`| string | none | Files under <webserver_doc_root> are accessible via the debug webserver.
`--create_cluster`| boolean |   | |
`--tserver_master_addrs` | string | none |Comma-separated list of all the RPC addresses for Master consensus-configuration. 
`--memory_limit_hard_bytes`| string | none  | Maximum amount of memory this daemon should use, in bytes. A value of 0 autosizes based on the total system memory. A value of -1 disables all memory limiting.
`--redis_proxy_webserver_port`| string | none | 
`--redis_proxy_bind_address`| string | none  | 
`--cql_proxy_webserver_port`| string | none  | 
`--cql_proxy_bind_address`| string | none | 
`--local_ip_for_outbound_sockets`| string | none | IP to bind to when making outgoing socket connections. This must be an IP address of the form A.B.C.D, not a hostname.
`--flagfile`| string | none  | Load flags from the specified file.
`--version` | string | false | Show version and build info

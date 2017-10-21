---
date: 2016-03-09T00:11:02+01:00
title: yb-tserver 
weight: 244
---

`yb-tserver`, located in the bin directory of YugaByte home, is the [YB-TServer](/architecture/concepts/#yb-tserver) binary.

## Help command

Use the **-\-help** option to see all the commands supported.

```sh
$ ./bin/yb-tserver --help
```

## Supported config flags

Flag | Mandatory | Default | Description 
----------------------|------|---------|------------------------
`--tserver_master_addrs` | Y | N/A  |Comma-separated list of all the `yb-master` RPC addresses.  
`--fs_data_dirs` | Y | N/A | Comma-separated list of directories where the `yb-tserver` will place it's `yb-data/tserver` data directory. 
`--fs_wal_dirs`| N | Same value as `--fs_data_dirs` | The directory where the `yb-tserver` will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory. 
`--log_dir`| N | Same value as `--fs_data_dirs`   | The directory to store `yb-tserver` log files.  
`--rpc_bind_addresses`| N |`0.0.0.0:9100` | Comma-separated list of addresses to bind to for RPC connections.
`--webserver_port`| N | `7000` | Monitoring web server port
`--webserver_doc_root`| N | The `www` directory in the YugaByte home directory | Monitoring web server home
`--cql_proxy_bind_address`| N | 9042 | CQL port
`--cql_proxy_webserver_port`| N | 12000 | CQL metrics monitoring port
`--redis_proxy_bind_address`| N | 6379  | Redis port
`--redis_proxy_webserver_port`| N | 11000 | Redis metrics monitoring port
`--flagfile`| N | N/A  | Load flags from the specified file.
`--version` | N | N/A | Show version and build info

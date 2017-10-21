---
date: 2016-03-09T00:11:02+01:00
title: yb-master
weight: 243
---

`yb-master`, located in the bin directory of YugaByte home, is the [YB-Master] (/architecture/concepts/#yb-master) binary.

## Help command

Use the **-\-help** option to see all the commands supported.

```sh
$ ./bin/yb-master --help
```

## Supported config flags

Flag | Mandatory | Default | Description 
----------------------|------|---------|------------------------
`--master_addresses` | Y | N/A |Comma-separated list of all the RPC addresses for `yb-master` consensus-configuration. 
`--fs_data_dirs` | Y | N/A | Comma-separated list of directories where the `yb-master` will place all it's `yb-data/master` data directory. 
`--fs_wal_dirs`| N | Same value as `--fs_data_dirs` | The directory where the `yb-master` will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory. 
`--log_dir`| N | Same value as `--fs_data_dirs`   | The directory to store `yb-master` log files.  
`--rpc_bind_addresses`| N |`0.0.0.0:7100` | Comma-separated list of addresses to bind to for RPC connections.
`--webserver_port`| N | `7000` | Monitoring web server port
`--webserver_doc_root`| N | The `www` directory in the YugaByte home directory | Monitoring web server home
`--replication_factor`| N |`3`  | Number of replicas to store for each tablet in the universe.
`--flagfile`| N | N/A  | Load flags from the specified file.
`--version` | N | N/A | Show version and build info


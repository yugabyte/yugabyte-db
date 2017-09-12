---
date: 2016-03-09T00:11:02+01:00
title: yb-master Reference
weight: 15
---

`yb-master`, located in the bin directory of YugaByte home, is the [YB-Master] (/architecture/concepts/#yb-master) binary.

## Help command

Use the **-\-help** option to see all the commands supported.

```sh
$ ./bin/yb-master --help
```

## Supported config flags

Flag | Type | Default | Description 
-------------------|-------------|---------|-------------------------------------------------------
`--fs_data_dirs` | string | none | Comma-separated list of directories where the YB-Master will place all its data. 
`--fs_wal_dirs`| string | If not specified, then same as `--fs_data_dirs` | The directory where the Master will place its write-ahead logs. May be the same as one of the directories listed in `--fs_data_dirs`, but not a sub-directory of a data directory. 
`--log_dir`| string | If not specified, then same as the first value in `--fs_data_dirs`   | The directory to store YB-Master log files.  
`--webserver_port`| string |  `8051` |
`--rpc_bind_addresses`| string | `0.0.0.0` | Comma-separated list of addresses to bind to for RPC connections.
`--webserver_doc_root`| string | none | Files under <webserver_doc_root> are accessible via the debug webserver.
`--create_cluster`| boolean |   |
`--master_addresses` | string | none |Comma-separated list of all the RPC addresses for YB-Master consensus-configuration. 
`--default_num_replicas`| string | `3`  | Number of replicas to store for each tablet.
`--flagfile`| string | none  | Load flags from the specified file.
`--version` | string | false | Show version and build info


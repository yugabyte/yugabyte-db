## yba xcluster create

Create an asynchronous replication config in YugabyteDB Anywhere

### Synopsis

Create an asynchronous replication config in YugabyteDB Anywhere

```
yba xcluster create [flags]
```

### Examples

```
yba xcluster create --name <xcluster-name> \
	--source-universe-name <source-universe-name> \
	--target-universe-name <target-universe-name> \
	--table-uuids <uuid-1>,<uuid-2>,<uuid-3> \
	--storage-config-name <storage-config-name>
```

### Options

```
  -n, --name string                          [Required] Name of the xcluster config to create. The name of the replication config cannot contain [SPACE '_' '*' '<' '>' '?' '|' '"' NULL] characters.
      --source-universe-name string          [Required] The name of the source universe for the xcluster config.
      --target-universe-name string          [Required] The name of the target universe for the xcluster config.
      --table-type string                    [Optional] Table type. Required when table-uuids is not specified. Allowed values: ysql, ycql
      --table-uuids string                   [Optional] Comma separated list of source universe table IDs/UUIDs. All tables must be of the same type. Run "yba universe table list --name <source-universe-name> --xcluster-supported-only" to check the list of tables that can be added for asynchronous replication. If left empty, all tables of specified table-type will be added for asynchronous replication.
      --storage-config-name string           [Required] Storage config to be used for taking the backup for replication. 
      --tables-need-full-copy-uuids string   [Optional] Comma separated list of source universe table IDs/UUIDs that are allowed to be full-copied to the target universe. Must be a subset of table-uuids. If left empty, allow-bootstrap is set to true so full-copy can be done for all the tables passed in to be in replication. Run "yba xcluster needs-full-copy-tables --source-universe-name <source-universe-name> --target-universe-name <target-universe-name> --table-uuids <tables-from-table-uuids-flag>" to check the list of tables that need bootstrapping.
      --parallelism int                      [Optional] Number of concurrent commands to run on nodes over SSH via "yb_backup" script. (default 8)
      --allow-bootstrap                      Allow full copy on all the tables being added to the replication. The same as passing the same set passed to table-uuids to tables-need-full-copy-uuids. (default false)
      --config-type string                   [Optional] Scope of the xcluster config to create. Allowed values: basic, txn, db. (default "basic")
      --dry-run                              [Optional] Run the pre-checks without actually running the subtasks. (default false)
  -h, --help                                 help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba xcluster](yba_xcluster.md)	 - Manage YugabyteDB Anywhere xClusters


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
      --config-type string                   [Optional] Scope of the xcluster config to create. Allowed values: basic, txn, db. (default "basic")
      --table-uuids string                   [Optional] Comma separated list of source universe table IDs/UUIDs. All tables must be of the same type. Run "yba universe table list --name <source-universe-name> --xcluster-supported-only" to check the list of tables that can be added for asynchronous replication. If left empty, all tables of specified table-type will be added for asynchronous replication.
      --skip-full-copy-tables                [Optional] Skip taking a backup for replication. (default false)
      --storage-config-name string           [Optional] Storage config to be used for taking the backup for replication. Required when tables require full copy. Ignored when skip-full-copy-tables is set to true.
      --tables-need-full-copy-uuids string   [Optional] Comma separated list of source universe table IDs/UUIDs that are allowed to be full-copied to the target universe. Must be a subset of table-uuids. If left empty, allow-full-copy-tables is set to true so full-copy can be done for all the tables passed in to be in replication. Run "yba xcluster needs-full-copy-tables --source-universe-name <source-universe-name> --target-universe-name <target-universe-name> --table-uuids <tables-from-table-uuids-flag>" to check the list of tables that need full copy. Ignored when skip-full-copy-tables is set to true.
      --allow-full-copy-tables               [Optional] Allow full copy on all the tables being added to the replication. The same as passing the same set passed to table-uuids to tables-need-full-copy-uuids. Ignored when skip-full-copy-tables is set to true. (default false)
      --parallelism int                      [Optional] Number of concurrent commands to run on nodes over SSH via "yb_backup" script. Ignored when skip-full-copy-tables is set to true. (default 8) (default 8)
      --dry-run                              [Optional] Run the pre-checks without actually running the subtasks. (default false)
  -h, --help                                 help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba xcluster](yba_xcluster.md)	 - Manage YugabyteDB Anywhere xClusters


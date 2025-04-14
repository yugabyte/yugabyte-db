## yba xcluster needs-full-copy-tables

Check whether source universe tables need full copy before setting up xCluster replication

### Synopsis

Check whether source universe tables need full copy before setting up xCluster replication

```
yba xcluster needs-full-copy-tables [flags]
```

### Examples

```
yba xcluster needs-full-copy-tables --source-universe-name <source-universe-name> \
	 --target-universe-name <target-universe-name> \
	 --table-uuids <uuid-1>,<uuid-2>,<uuid-3>
```

### Options

```
      --source-universe-name string   [Required] The name of the source universe for the xcluster.
      --target-universe-name string   [Optional] The name of the target universe for the xcluster. If tables do not exist on the target universe, full copy is required. If not specified, only source table is checked for data.
      --table-uuids string            [Optional] The IDs/UUIDs of the source universe tables that need to checked for full copy. If left empty, all tables on the source universe will be checked.
  -h, --help                          help for needs-full-copy-tables
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


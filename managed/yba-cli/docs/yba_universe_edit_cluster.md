## yba universe edit cluster

Edit clusters in a YugabyteDB Anywhere universe

### Synopsis

Edit clusters in a universe in YugabyteDB Anywhere

```
yba universe edit cluster [flags]
```

### Options

```
  -h, --help   help for cluster
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe to be edited.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe edit](yba_universe_edit.md)	 - Edit a YugabyteDB Anywhere universe
* [yba universe edit cluster primary](yba_universe_edit_cluster_primary.md)	 - Edit the Primary Cluster in a YugabyteDB Anywhere universe
* [yba universe edit cluster read-replica](yba_universe_edit_cluster_read-replica.md)	 - Edit the Read replica Cluster in a YugabyteDB Anywhere universe
* [yba universe edit cluster smart-resize](yba_universe_edit_cluster_smart-resize.md)	 - Edit the Volume size or instance type of Primary Cluster nodes in a YugabyteDB Anywhere universe using smart resize.


## yba universe edit cluster smart-resize

Edit the Volume size or instance type of Primary Cluster nodes in a YugabyteDB Anywhere universe using smart resize.

### Synopsis

Edit the volume size or instance type of Primary Cluster nodes in a YugabyteDB Anywhere universe using smart resize.

```
yba universe edit cluster smart-resize [flags]
```

### Examples

```
yba universe edit cluster smart-resize \
  -n <universe-name> --volume-size <volume-size>
```

### Options

```
      --instance-type string                    [Optional] Instance Type for the universe nodes.
      --volume-size int                         [Optional] The size of each volume in each instance.
      --dedicated-master-instance-type string   [Optional] Instance Type for the each master instance. Applied only when universe has dedicated master nodes.
      --dedicated-master-volume-size int        [Optional] The size of each volume in each master instance. Applied only when universe has dedicated master nodes.
      --delay-between-master-servers int32      [Optional] Upgrade delay between Master servers (in miliseconds). (default 18000)
      --delay-between-tservers int32            [Optional] Upgrade delay between Tservers (in miliseconds). (default 18000)
  -h, --help                                    help for smart-resize
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

* [yba universe edit cluster](yba_universe_edit_cluster.md)	 - Edit clusters in a YugabyteDB Anywhere universe


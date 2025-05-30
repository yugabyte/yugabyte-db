## yba backup pitr create

Create a new PITR configuration for the universe

### Synopsis

Create Point-In-Time Recovery (PITR) configuration for a keyspace in the universe

```
yba backup pitr create [flags]
```

### Examples

```
yba backup pitr create --universe-name <universe-name> --keyspace <keyspace-name>
	--table-type <table-type> --retention-in-secs <retention-in-secs>
```

### Options

```
  -k, --keyspace string                 [Required] The name of the keyspace for which PITR config needs to be created.
  -t, --table-type string               [Required] The table type for which PITR config needs to be created. Supported values: ycql, ysql
  -r, --retention-in-secs int           [Required] The retention period in seconds for the PITR config.
  -s, --schedule-interval-in-secs int   [Optional] The schedule interval in seconds for the PITR config. (default 86400)
  -h, --help                            help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string        YugabyteDB Anywhere api token.
      --ca-cert string         CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string          Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug                  Use debug mode, same as --logLevel debug.
      --directory string       Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color          Disable colors in output. (default false)
  -H, --host string            YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure               Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string        Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string          Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration       Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --universe-name string   [Required] The name of the universe associated with the PITR configuration.
      --wait                   Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba backup pitr](yba_backup_pitr.md)	 - Manage YugabyteDB Anywhere universe PITR configs


## yba backup pitr

Manage YugabyteDB Anywhere universe PITR configs

### Synopsis

Manage YugabyteDB Anywhere universe PITR (Point In Time Recovery) configs

```
yba backup pitr [flags]
```

### Options

```
      --universe-name string   [Required] The name of the universe associated with the PITR configuration.
  -h, --help                   help for pitr
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

* [yba backup](yba_backup.md)	 - Manage YugabyteDB Anywhere universe backups
* [yba backup pitr create](yba_backup_pitr_create.md)	 - Create a new PITR configuration for the universe
* [yba backup pitr delete](yba_backup_pitr_delete.md)	 - Delete the PITR configuration for the universe
* [yba backup pitr edit](yba_backup_pitr_edit.md)	 - Edit the existing PITR configuration for the universe
* [yba backup pitr list](yba_backup_pitr_list.md)	 - List PITR configurations for the universe
* [yba backup pitr recover](yba_backup_pitr_recover.md)	 - Recover universe keyspace to a point in time


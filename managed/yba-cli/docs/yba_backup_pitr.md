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

* [yba backup](yba_backup.md)	 - Manage YugabyteDB Anywhere universe backups
* [yba backup pitr create](yba_backup_pitr_create.md)	 - Create a new PITR configuration for the universe
* [yba backup pitr delete](yba_backup_pitr_delete.md)	 - Delete the PITR configuration for the universe
* [yba backup pitr edit](yba_backup_pitr_edit.md)	 - Edit the existing PITR configuration for the universe
* [yba backup pitr list](yba_backup_pitr_list.md)	 - List PITR configurations for the universe
* [yba backup pitr recover](yba_backup_pitr_recover.md)	 - Recover universe keyspace to a point in time


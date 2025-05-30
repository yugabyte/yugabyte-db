## yba backup

Manage YugabyteDB Anywhere universe backups

### Synopsis

Manage YugabyteDB Anywhere universe backups

```
yba backup [flags]
```

### Options

```
  -h, --help   help for backup
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba backup create](yba_backup_create.md)	 - Create a YugabyteDB Anywhere universe backup
* [yba backup delete](yba_backup_delete.md)	 - Delete a YugabyteDB Anywhere universe backup
* [yba backup describe](yba_backup_describe.md)	 - Describe a YugabyteDB Anywhere universe backup
* [yba backup list](yba_backup_list.md)	 - List YugabyteDB Anywhere backups
* [yba backup list-increments](yba_backup_list-increments.md)	 - List the incremental backups of a backup
* [yba backup pitr](yba_backup_pitr.md)	 - Manage YugabyteDB Anywhere universe PITR configs
* [yba backup restore](yba_backup_restore.md)	 - Manage YugabyteDB Anywhere universe backup restores
* [yba backup schedule](yba_backup_schedule.md)	 - Manage YugabyteDB Anywhere universe backup schedules
* [yba backup update](yba_backup_update.md)	 - Edit a YugabyteDB Anywhere universe backup


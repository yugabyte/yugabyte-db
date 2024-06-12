## yba backup edit

Edit a YugabyteDB Anywhere universe backup

### Synopsis

Edit an universe backup in YugabyteDB Anywhere

```
yba backup edit [flags]
```

### Options

```
      --backup-uuid string             [Required] The uuid of the backup to be described.
      --time-before-delete-in-ms int   [Required] Time before delete from the current time in ms
      --storage-config-name string     [Optional] Change the storage config assigned to the backup
  -h, --help                           help for edit
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

* [yba backup](yba_backup.md)	 - Manage YugabyteDB Anywhere backups


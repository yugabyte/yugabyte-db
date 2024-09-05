## yba backup delete

Delete a YugabyteDB Anywhere universe backup

### Synopsis

Delete an universe backup in YugabyteDB Anywhere

```
yba backup delete [flags]
```

### Options

```
      --backup-info stringArray   [Required] The info of the backups to be described. The backup-info is of the format backup-uuid=<backup_uuid>,storage-config-uuid=<storage-config-uuid>. Backup UUID is required..
  -h, --help                      help for delete
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


## yba backup schedule

Manage YugabyteDB Anywhere universe backup schedules

### Synopsis

Manage YugabyteDB Anywhere universe backup schedules

```
yba backup schedule [flags]
```

### Options

```
  -h, --help   help for schedule
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
* [yba backup schedule create](yba_backup_schedule_create.md)	 - Create a YugabyteDB Anywhere universe backup schedule
* [yba backup schedule delete](yba_backup_schedule_delete.md)	 - Delete a YugabyteDB Anywhere universe backup schedule
* [yba backup schedule describe](yba_backup_schedule_describe.md)	 - Describe a YugabyteDB Anywhere universe backup schedule
* [yba backup schedule list](yba_backup_schedule_list.md)	 - List YugabyteDB Anywhere universe backup schedules


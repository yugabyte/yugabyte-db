## yba alert maintenance-window

Manage YugabyteDB Anywhere maintenance window

### Synopsis

Manage YugabyteDB Anywhere maintenance window

```
yba alert maintenance-window [flags]
```

### Options

```
  -h, --help   help for maintenance-window
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

* [yba alert](yba_alert.md)	 - Manage YugabyteDB Anywhere alerts
* [yba alert maintenance-window create](yba_alert_maintenance-window_create.md)	 - Create a maintenance window to suppress alerts
* [yba alert maintenance-window delete](yba_alert_maintenance-window_delete.md)	 - Delete a YugabyteDB Anywhere maintenance window
* [yba alert maintenance-window describe](yba_alert_maintenance-window_describe.md)	 - Describe a YugabyteDB Anywhere maintenance window
* [yba alert maintenance-window list](yba_alert_maintenance-window_list.md)	 - List YugabyteDB Anywhere maintenance windows
* [yba alert maintenance-window update](yba_alert_maintenance-window_update.md)	 - Update a maintenance window to suppress alerts


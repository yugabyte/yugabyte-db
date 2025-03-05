## yba alert destination

Manage YugabyteDB Anywhere alert destinations

### Synopsis

Manage YugabyteDB Anywhere alert destinations (group of channels)

```
yba alert destination [flags]
```

### Options

```
  -h, --help   help for destination
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
* [yba alert destination create](yba_alert_destination_create.md)	 - Create an alert destination in YugabyteDB Anywhere
* [yba alert destination delete](yba_alert_destination_delete.md)	 - Delete YugabyteDB Anywhere alert destination
* [yba alert destination describe](yba_alert_destination_describe.md)	 - Describe a YugabyteDB Anywhere alert destination
* [yba alert destination list](yba_alert_destination_list.md)	 - List YugabyteDB Anywhere alert destinations
* [yba alert destination update](yba_alert_destination_update.md)	 - Update an alert destination in YugabyteDB Anywhere


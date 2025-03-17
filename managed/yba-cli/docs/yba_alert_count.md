## yba alert count

Count YugabyteDB Anywhere alerts

### Synopsis

Count alerts in YugabyteDB Anywhere

```
yba alert count [flags]
```

### Examples

```
yba alert count 
```

### Options

```
      --configuration-uuid string    [Optional] Configuration UUID to filter alerts.
      --configuration-types string   [Optional] Comma separated list of configuration types.
      --severities string            [Optional] Comma separated list of severities. Allowed values: severe, warning.
      --source-uuids string          [Optional] Comma separated list of source UUIDs.
      --source-name string           [Optional] Source name to filter alerts.
      --states string                [Optional] Comma separated list of states. Allowed values: active, acknowledged, suspended, resolved.
      --uuids string                 [Optional] Comma separated list of alert UUIDs.
  -h, --help                         help for count
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


## yba alert list

List YugabyteDB Anywhere alerts

### Synopsis

List alerts in YugabyteDB Anywhere

```
yba alert list [flags]
```

### Examples

```
yba alert list --source-name <source-name>
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
      --sorting-field string         [Optional] Field to sort alerts. Allowed values: severity, create-time, source-name, name, state, uuid. (default "create-time")
      --direction string             [Optional] Direction to sort alerts. Allowed values: asc, desc. (default "desc")
  -f, --force                        [Optional] Bypass the prompt for non-interactive usage.
  -h, --help                         help for list
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


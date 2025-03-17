## yba alert policy template list

List YugabyteDB Anywhere alert policy templates

### Synopsis

List YugabyteDB Anywhere alert policy templates

```
yba alert policy template list [flags]
```

### Examples

```
yba alert policy template list
```

### Options

```
  -n, --name string          [Optional] Name of the alert template.
      --target-type string   [Optional] Target type of the alert template. Allowed values: platform, universe.
  -h, --help                 help for list
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

* [yba alert policy template](yba_alert_policy_template.md)	 - Manage YugabyteDB Anywhere alert templates


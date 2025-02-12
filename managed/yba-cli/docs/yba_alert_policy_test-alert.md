## yba alert policy test-alert

Send a test alert corresponding to YugabyteDB Anywhere alert policy

### Synopsis

Send a test alert corresponding to an alert policy in YugabyteDB Anywhere

```
yba alert policy test-alert [flags]
```

### Examples

```
yba alert policy test-alert --name <alert-configuration-name>
```

### Options

```
  -n, --name string   [Required] Name of alert policy to test alerts. Use single quotes ('') to provide values with special characters.
  -h, --help          help for test-alert
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

* [yba alert policy](yba_alert_policy.md)	 - Manage YugabyteDB Anywhere alert policies


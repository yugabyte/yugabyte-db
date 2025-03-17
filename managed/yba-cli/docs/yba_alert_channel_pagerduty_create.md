## yba alert channel pagerduty create

Create a PagerDuty alert channel in YugabyteDB Anywhere

### Synopsis

Create a PagerDuty alert channel in YugabyteDB Anywhere

```
yba alert channel pagerduty create [flags]
```

### Examples

```
yba alert channel pagerduty create --name <alert-channel-name> \
   --pagerduty-api-key <pagerduty-pagerduty-api-key> --routing-key <pagerduty-routing-key>
```

### Options

```
      --pagerduty-api-key string   [Required] PagerDuty API key.
      --routing-key string         [Required] PagerDuty routing key.
  -h, --help                       help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the alert channel for the operation. Use single quotes ('') to provide values with special characters. Required for create, update, describe, delete.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba alert channel pagerduty](yba_alert_channel_pagerduty.md)	 - Manage YugabyteDB Anywhere PagerDuty alert notification channels


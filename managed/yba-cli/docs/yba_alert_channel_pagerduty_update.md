## yba alert channel pagerduty update

Update a YugabyteDB Anywhere PagerDuty alert channel

### Synopsis

Update a PagerDuty alert channel in YugabyteDB Anywhere

```
yba alert channel pagerduty update [flags]
```

### Examples

```
yba alert channel pagerduty update --name <channel-name> \
   --new-name <new-channel-name> --pagerduty-api-key <pagerduty-api-key> \
   --routing-key <pagerduty-routing-key>
```

### Options

```
      --new-name string            [Optional] Update name of the alert channel.
      --pagerduty-api-key string   [Optional] Update PagerDuty API key.
      --routing-key string         [Optional] Update PagerDuty routing key.
  -h, --help                       help for update
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


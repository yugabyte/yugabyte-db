## yba alert channel slack update

Update a YugabyteDB Anywhere Slack alert channel

### Synopsis

Update a Slack alert channel in YugabyteDB Anywhere

```
yba alert channel slack update [flags]
```

### Examples

```
yba alert channel slack update --name <channel-name> \
  --new-name <new-channel-name> --username <slack-username> \
  --webhook-url <slack-webhook-url> --icon-url <slack-icon-url>
```

### Options

```
      --new-name string      [Optional] Update name of the alert channel.
      --username string      [Optional] Update slack username.
      --webhook-url string   [Optional] Update slack webhook URL.
      --icon-url string      [Optional] Update slack icon URL.
  -h, --help                 help for update
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

* [yba alert channel slack](yba_alert_channel_slack.md)	 - Manage YugabyteDB Anywhere slack alert notification channels


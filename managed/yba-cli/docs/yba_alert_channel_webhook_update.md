## yba alert channel webhook update

Update a YugabyteDB Anywhere Webhook alert channel

### Synopsis

Update a Webhook alert channel in YugabyteDB Anywhere

```
yba alert channel webhook update [flags]
```

### Examples

```
yba alert channel webhook update --name <channel-name> \
   --new-name <new-channel-name> --auth-type <webhook-auth-type> --username <webhook-username> \
   --password <password>
```

### Options

```
      --new-name string               [Optional] Update name of the alert channel.
      --webhook-url string            [Optional] Update webhook URL.
      --auth-type string              [Optional] Update authentication type for webhook. Allowed values: none, basic, token.
      --username string               [Optional] Update username for authethicating webhook. Required if auth type is basic.
      --password string               [Optional] Update password for authethicating webhook. Required if auth type is basic.
      --token-header string           [Optional] Update token header for authethicating webhook. Required if auth type is token.
      --token-value string            [Optional] Update token value for authethicating webhook. Required if auth type is token.
      --send-resolved-notifications   [Optional] Send resolved notifications for alert channel. (default true)
  -h, --help                          help for update
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

* [yba alert channel webhook](yba_alert_channel_webhook.md)	 - Manage YugabyteDB Anywhere webhook alert notification channels


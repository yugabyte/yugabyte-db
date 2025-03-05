## yba alert channel webhook

Manage YugabyteDB Anywhere webhook alert notification channels

### Synopsis

Manage YugabyteDB Anywhere webhook alert notification channels 

```
yba alert channel webhook [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the alert channel for the operation. Use single quotes ('') to provide values with special characters. Required for create, update, describe, delete.
  -h, --help          help for webhook
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

* [yba alert channel](yba_alert_channel.md)	 - Manage YugabyteDB Anywhere alert notification channels
* [yba alert channel webhook create](yba_alert_channel_webhook_create.md)	 - Create a webhook alert channel in YugabyteDB Anywhere
* [yba alert channel webhook delete](yba_alert_channel_webhook_delete.md)	 - Delete YugabyteDB Anywhere webhook alert channel
* [yba alert channel webhook describe](yba_alert_channel_webhook_describe.md)	 - Describe a YugabyteDB Anywhere webhook alert channel
* [yba alert channel webhook list](yba_alert_channel_webhook_list.md)	 - List YugabyteDB Anywhere webhook alert channels
* [yba alert channel webhook update](yba_alert_channel_webhook_update.md)	 - Update a YugabyteDB Anywhere Webhook alert channel


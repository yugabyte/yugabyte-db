## yba alert channel slack

Manage YugabyteDB Anywhere slack alert notification channels

### Synopsis

Manage YugabyteDB Anywhere slack alert notification channels 

```
yba alert channel slack [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the alert channel for the operation. Use single quotes ('') to provide values with special characters. Required for create, update, describe, delete.
  -h, --help          help for slack
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba alert channel](yba_alert_channel.md)	 - Manage YugabyteDB Anywhere alert notification channels
* [yba alert channel slack create](yba_alert_channel_slack_create.md)	 - Create a slack alert channel in YugabyteDB Anywhere
* [yba alert channel slack delete](yba_alert_channel_slack_delete.md)	 - Delete YugabyteDB Anywhere slack alert channel
* [yba alert channel slack describe](yba_alert_channel_slack_describe.md)	 - Describe a YugabyteDB Anywhere slack alert channel
* [yba alert channel slack list](yba_alert_channel_slack_list.md)	 - List YugabyteDB Anywhere slack alert channels
* [yba alert channel slack update](yba_alert_channel_slack_update.md)	 - Update a YugabyteDB Anywhere Slack alert channel


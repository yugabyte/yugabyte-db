## yba alert channel slack delete

Delete YugabyteDB Anywhere slack alert channel

### Synopsis

Delete an slack alert channel in YugabyteDB Anywhere

```
yba alert channel slack delete [flags]
```

### Examples

```
yba alert channel slack delete --name <alert-channel-name>
```

### Options

```
  -f, --force   [Optional] Bypass the prompt for non-interactive usage.
  -h, --help    help for delete
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


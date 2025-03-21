## yba alert channel describe

Describe a YugabyteDB Anywhere alert channel

### Synopsis

Describe an alert channel in YugabyteDB Anywhere

```
yba alert channel describe [flags]
```

### Examples

```
yba alert channel describe --name <alert-channel-name>
```

### Options

```
  -n, --name string   [Required] The name of the alert channel to be described.
  -h, --help          help for describe
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


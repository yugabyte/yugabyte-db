## yba alert destination update

Update an alert destination in YugabyteDB Anywhere

### Synopsis

Update an alert destination in YugabyteDB Anywhere

```
yba alert destination update [flags]
```

### Examples

```
yba alert destination update --name <alert-destination-name> \
     --set-default \
     --add-channel <alert-channel-1>,<alert-channel-2>
```

### Options

```
  -n, --name string             [Required] Name of alert destination to update. Use single quotes ('') to provide values with special characters.
      --new-name string         [Optional] Update name of alert destination.
      --set-default             [Optional] Set this alert destination as default.
      --add-channel string      [Optional] Add alert channels to destination. Provide the comma separated channel names. Use single quotes ('') to provide values with special characters.
      --remove-channel string   [Optional] Provide the comma separated channel names to be removed from destination. Use single quotes ('') to provide values with special characters.
  -h, --help                    help for update
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

* [yba alert destination](yba_alert_destination.md)	 - Manage YugabyteDB Anywhere alert destinations


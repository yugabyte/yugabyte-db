## yba alert channel email update

Update a YugabyteDB Anywhere Email alert channel

### Synopsis

Update a Email alert channel in YugabyteDB Anywhere

```
yba alert channel email update [flags]
```

### Examples

```
yba alert channel email update --name <channel-name> \
   --new-name <new-channel-name> --use-default-recipients --recipients <recipients>
```

### Options

```
      --new-name string             [Optional] Update name of the alert channel.
      --use-default-rececipients    [Optional] Update use default recipients for alert channel.
      --recipients stringArray      [Optional] Update recipients for alert channel.
      --use-default-smtp-settings   [Optional] Update use default SMTP settings for alert channel.
      --smtp-server string          [Optional] Update SMTP server for alert channel.
      --smtp-port int               [Optional] Update SMTP port for alert channel. (default -1)
      --email-from string           [Optional] Update email from for alert channel.
      --smtp-username string        [Optional] Update SMTP username.
      --smtp-password string        [Optional] Update SMTP password.
      --use-ssl                     [Optional] Update use SSL for SMTP connection.
      --use-tls                     [Optional] Update use TLS for SMTP connection.
  -h, --help                        help for update
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

* [yba alert channel email](yba_alert_channel_email.md)	 - Manage YugabyteDB Anywhere email alert notification channels


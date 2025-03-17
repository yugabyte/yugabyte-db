## yba alert channel email create

Create a email alert channel in YugabyteDB Anywhere

### Synopsis

Create a email alert channel in YugabyteDB Anywhere

```
yba alert channel email create [flags]
```

### Examples

```
yba alert channel email create --name <alert-channel-name> \
  --use-default-recipients --use-default-smtp-settings
```

### Options

```
      --use-default-rececipients    [Optional] Use default recipients for alert channel. (default false)
      --recipients stringArray      [Optional] Recipients for alert channel. Can be provided as separate flags or as comma-separated values. Required when use-default-recipients is false
      --use-default-smtp-settings   [Optional] Use default SMTP settings for alert channel. Values of smtp-server, smtp-port, email-from, smtp-username, smtp-password, use-ssl, use-tls are used if false. (default false)
      --smtp-server string          [Optional] SMTP server for alert channel. If smtp-server is empty, runtime configuration value "yb.health.default_smtp_server" is used.
      --smtp-port int               [Optional] SMTP port for alert channel. If smtp-port is -1, runtime configuration value "yb.health.default_smtp_port" is used for non SSL connection and "yb.health.default_smtp_port_ssl" is used for SSL connection. (default -1)
      --email-from string           [Optional] SMTP email 'from' address. Required when use-default-smtp-settings is false
      --smtp-username string        [Optional] SMTP username.
      --smtp-password string        [Optional] SMTP password.
      --use-ssl                     [Optional] Use SSL for SMTP connection. (default false)
      --use-tls                     [Optional] Use TLS for SMTP connection. (default false)
  -h, --help                        help for create
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


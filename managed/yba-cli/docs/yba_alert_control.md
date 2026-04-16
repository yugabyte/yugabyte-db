## yba alert control

Manage default alert controls

### Synopsis

Manage default alert controls

```
yba alert control [flags]
```

### Options

```
      --callhome-level string                    [Optional] Manage callhome level. Allowed values: none, low, medium, high.
      --health-check-interval int                [Optional] Health check intervals in minutes. Value below 5 minutes is not allowed. (default 5)
      --active-alert-notification-interval int   [Optional] Period which is used to send active alert notifications in minutes.
      --email-alerts string                      [Optional] Manage email notifications. Allowed values: enable, disable.
      --default-recipients stringArray           [Optional] Edit default recipients for email notifications. Can be provided as separate flags or as comma-separated values. Required when email-alerts is enabled.
      --send-emails-to-yb-team                   [Optional] Send emails to YugabyteDB team. (default false)
      --email-from string                        [Optional] Email address to send alerts from. Required when email-alerts is enabled
      --smtp-server string                       [Optional] SMTP server address. Required when email-alerts is enabled
      --smtp-port int                            [Optional] SMTP server port. Required when email-alerts is enabled (default -1)
      --smtp-username string                     [Optional] SMTP server username.
      --smtp-password string                     [Optional] SMTP server password.
      --use-ssl                                  [Optional] Use SSL for SMTP connection. (default false)
      --use-tls                                  [Optional] Use TLS for SMTP connection. (default false)
      --health-check-email-interval int          [Optional] Period between health check email notifications in minutes.
      --include-only-errors-in-email             [Optional] Include only errors in email notifications. (default false)
  -h, --help                                     help for control
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

* [yba alert](yba_alert.md)	 - Manage YugabyteDB Anywhere alerts


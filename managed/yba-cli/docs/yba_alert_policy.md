## yba alert policy

Manage YugabyteDB Anywhere alert policies

### Synopsis

Manage YugabyteDB Anywhere alert policies

```
yba alert policy [flags]
```

### Options

```
  -h, --help   help for policy
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
* [yba alert policy create](yba_alert_policy_create.md)	 - Create an alert policy in YugabyteDB Anywhere
* [yba alert policy delete](yba_alert_policy_delete.md)	 - Delete YugabyteDB Anywhere alert policy
* [yba alert policy describe](yba_alert_policy_describe.md)	 - Describe YugabyteDB Anywhere alert policy
* [yba alert policy list](yba_alert_policy_list.md)	 - List YugabyteDB Anywhere alert policies
* [yba alert policy template](yba_alert_policy_template.md)	 - Manage YugabyteDB Anywhere alert templates
* [yba alert policy test-alert](yba_alert_policy_test-alert.md)	 - Send a test alert corresponding to YugabyteDB Anywhere alert policy
* [yba alert policy update](yba_alert_policy_update.md)	 - Update an alert policy in YugabyteDB Anywhere


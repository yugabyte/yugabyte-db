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

* [yba alert](yba_alert.md)	 - Manage YugabyteDB Anywhere alerts
* [yba alert policy create](yba_alert_policy_create.md)	 - Create an alert policy in YugabyteDB Anywhere
* [yba alert policy delete](yba_alert_policy_delete.md)	 - Delete YugabyteDB Anywhere alert policy
* [yba alert policy describe](yba_alert_policy_describe.md)	 - Describe YugabyteDB Anywhere alert policy
* [yba alert policy list](yba_alert_policy_list.md)	 - List YugabyteDB Anywhere alert policies
* [yba alert policy template](yba_alert_policy_template.md)	 - Manage YugabyteDB Anywhere alert templates
* [yba alert policy test-alert](yba_alert_policy_test-alert.md)	 - Send a test alert corresponding to YugabyteDB Anywhere alert policy
* [yba alert policy update](yba_alert_policy_update.md)	 - Update an alert policy in YugabyteDB Anywhere


## yba alert policy create

Create an alert policy in YugabyteDB Anywhere

### Synopsis

Create an alert policy in YugabyteDB Anywhere

```
yba alert policy create [flags]
```

### Examples

```
yba alert policy create --name <alert-policy-name> \
		--alert-type <alert-type> --alert-severity <alert-severity> --alert-config <alert-config>
```

### Options

```
  -n, --name string               [Required] Name of alert policy to create. Use single quotes ('') to provide values with special characters.
      --description string        [Optional] Description of alert policy to create.
      --target-uuids string       [Optional] Comma separated list of target UUIDs for the alert policy. If left empty, the alert policy will be created for all targets of the target type. Allowed for target type: universe.
      --threshold stringArray     [Optional] Threshold for the configuration corresponding to severity. Each threshold needs to be added as a separate --threshold flag.Provide the following double colon (::) separated fields as key-value pairs: "severity=<severity>::condition=<condition>::threshold=<threshold>". Allowed values for severity: severe, warning. Allowed values for condition: greater-than, less-than, not-equal. Threshold should be a double. Example: "severity=severe::condition=greater-than::threshold=60000".
      --template string           [Required] Template name for the alert policy. Use single quotes ('') to provide values with special characters. Run "yba alert policy template list" to check list of available template names.
      --duration int              [Optional] Duration in seconds, while condition is met to raise an alert. (default 0)
      --destination-type string   [Optional] Destination type to create alert policy. Allowed values: no, default, selected
      --destination string        [Optional] Destination name to send alerts. Run "yba alert destination list" to check list of available destinations. Required if destination-type is selected.
  -h, --help                      help for create
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

* [yba alert policy](yba_alert_policy.md)	 - Manage YugabyteDB Anywhere alert policies


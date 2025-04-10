## yba alert policy update

Update an alert policy in YugabyteDB Anywhere

### Synopsis

Update an alert policy in YugabyteDB Anywhere

```
yba alert policy update [flags]
```

### Examples

```
yba alert policy update --name <alert-policy-name> \
		--alert-type <alert-type> --alert-severity <alert-severity> --alert-config <alert-config>
```

### Options

```
  -n, --name string                    [Required] Name of alert policy to update. Use single quotes ('') to provide values with special characters.
      --new-name string                [Optional] Update name of alert policy.
      --description string             [Optional] Update description of alert policy.
      --target-uuids string            [Optional] Comma separated list of target UUIDs for the alert policy. If empty string is specified, the alert policy will be updated for all targets of the target type. Allowed for target type: universe.
      --add-threshold stringArray      [Optional] Add or edit threshold for the configuration corresponding to severity.Each threshold needs to be added as a separate --add-threshold flag. Provide the following double colon (::) separated fields as key-value pairs: "severity=<severity>::condition=<condition>::threshold=<threshold>". Allowed values for severity: severe, warning. Allowed values for condition: greater-than, less-than, not-equal. Threshold should be a double. Example: "severity=severe::condition=greater-than::threshold=60000".
      --remove-threshold stringArray   [Optional] Provide the comma separated severities to be removed from threshold. Allowed values: severe, warning.
      --duration int                   [Optional] Update duration in seconds, while condition is met to raise an alert.
      --destination-type string        [Optional] Destination type to update alert policy. Allowed values: no, default, selected
      --destination string             [Optional] Destination name to send alerts. Run "yba alert destination list" to check list of available destinations. Required if destination-type is selected.
      --state string                   [Optional] Set state of the alert policy. Allowed values: enable, disable.)
  -h, --help                           help for update
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

* [yba alert policy](yba_alert_policy.md)	 - Manage YugabyteDB Anywhere alert policies


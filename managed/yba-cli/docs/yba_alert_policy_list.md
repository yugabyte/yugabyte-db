## yba alert policy list

List YugabyteDB Anywhere alert policies

### Synopsis

List alert policies in YugabyteDB Anywhere

```
yba alert policy list [flags]
```

### Examples

```
yba alert policy list --source-name <source-name>
```

### Options

```
      --active                    [Optional] Filter active alert policy. (default true)
      --destination-type string   [Optional] Destination type to filter alert policy. Allowed values: no, default, selected
      --destination string        [Optional] Destination name to filter alert policy. Required if destination-type is selected.
      --name string               [Optional] Name to filter alert policy.
      --severity string           [Optional] Severity to filter alert policy. Allowed values: severe, warning.
      --target-uuids string       [Optional] Comma separated list of target UUIDs for the alert policy.
      --target-type string        [Optional] Target type to filter alert policy. Allowed values: platform, universe.
      --template string           [Optional] Template type to filter alert policy. Allowed values (case-sensitive) are listed: https://github.com/yugabyte/yugabyte-db/blob/master/managed/src/main/java/com/yugabyte/yw/common/AlertTemplate.java
      --uuids string              [Optional] Comma separated list of alert policy UUIDs.
      --sorting-field string      [Optional] Field to sort alerts. Allowed values: uuid, name, active, target-type, target, create-time, template, severity, destination, alert-count. (default "name")
      --direction string          [Optional] Direction to sort alerts. Allowed values: asc, desc. (default "asc")
  -f, --force                     [Optional] Bypass the prompt for non-interactive usage.
  -h, --help                      help for list
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


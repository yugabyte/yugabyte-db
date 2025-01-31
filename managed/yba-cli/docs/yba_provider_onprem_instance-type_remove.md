## yba provider onprem instance-type remove

Delete instance type of a YugabyteDB Anywhere on-premises provider

### Synopsis

Delete instance types of a YugabyteDB Anywhere on-premises provider

```
yba provider onprem instance-type remove [flags]
```

### Examples

```
yba provider onprem instance-type remove \
	--name <provider-name> --instance-type-name <instance-type-name>
```

### Options

```
      --instance-type-name string   [Required] Instance type name.
  -f, --force                       [Optional] Bypass the prompt for non-interactive usage.
  -h, --help                        help for remove
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, instance-type and node.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider onprem instance-type](yba_provider_onprem_instance-type.md)	 - Manage YugabyteDB Anywhere onprem instance types


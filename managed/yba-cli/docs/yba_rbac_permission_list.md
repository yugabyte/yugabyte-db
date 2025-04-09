## yba rbac permission list

List YugabyteDB Anywhere permissions

### Synopsis

List YugabyteDB Anywhere permissions

```
yba rbac permission list [flags]
```

### Examples

```
yba rbac permission list
```

### Options

```
  -n, --name string            [Optional] Name of the permission. Quote name if it contains space.
      --resource-type string   [Optional] Resource type of the permission. Allowed values: universe, role, user, other. If not specified, all resource types are returned.
  -h, --help                   help for list
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba rbac permission](yba_rbac_permission.md)	 - Manage YugabyteDB Anywhere RBAC permissions


## yba rbac permission describe

Describe a YugabyteDB Anywhere RBAC permission

### Synopsis

Describe a RBAC permission in YugabyteDB Anywhere

```
yba rbac permission describe [flags]
```

### Examples

```
yba rbac permission describe --name <permission-name>
```

### Options

```
  -n, --name string   [Required] Name of the permission. Quote name if it contains space.
  -h, --help          help for describe
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

* [yba rbac permission](yba_rbac_permission.md)	 - Manage YugabyteDB Anywhere RBAC permissions


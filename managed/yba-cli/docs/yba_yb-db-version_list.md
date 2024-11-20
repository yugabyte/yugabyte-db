## yba yb-db-version list

List YugabyteDB versions

### Synopsis

List YugabyteDB versions

```
yba yb-db-version list [flags]
```

### Examples

```
yba yb-db-version list
```

### Options

```
      --deployment-type string   [Optional] Deployment type of the YugabyteDB version. Allowed values: x86_64, aarch64, kubernetes
      --type string              [Optional] Release type. Allowed values: lts, sts, preview
  -h, --help                     help for list
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

* [yba yb-db-version](yba_yb-db-version.md)	 - Manage YugabyteDB versions


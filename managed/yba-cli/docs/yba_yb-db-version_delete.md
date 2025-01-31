## yba yb-db-version delete

Delete a YugabyteDB version

### Synopsis

Delete a version in YugabyteDB Anywhere

```
yba yb-db-version delete [flags]
```

### Examples

```
yba yb-db-version delete --version <version>
```

### Options

```
  -v, --version string           [Required] The YugabyteDB version to be deleted.
      --deployment-type string   [Optional] Deployment type of the YugabyteDB version. Allowed values: x86_64, aarch64, kubernetes
  -f, --force                    [Optional] Bypass the prompt for non-interactive usage.
  -h, --help                     help for delete
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


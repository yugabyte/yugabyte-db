## yba yb-db-version architecture

Manage architectures for a version of YugabyteDB

### Synopsis

Manage architectures for a version of YugabyteDB

```
yba yb-db-version architecture [flags]
```

### Options

```
  -v, --version string   [Required] YugabyteDB version to be updated.
  -h, --help             help for architecture
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
* [yba yb-db-version architecture add](yba_yb-db-version_architecture_add.md)	 - Add architectures to a version of YugabyteDB
* [yba yb-db-version architecture edit](yba_yb-db-version_architecture_edit.md)	 - Edit architectures to a version of YugabyteDB


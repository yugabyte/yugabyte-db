## yba yb-db-version artifact-create

Fetch artifact metadata for a version of YugabyteDB

### Synopsis

Fetch artifact metadata for a version of YugabyteDB

```
yba yb-db-version artifact-create [flags]
```

### Options

```
  -h, --help   help for artifact-create
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
* [yba yb-db-version artifact-create url](yba_yb-db-version_artifact-create_url.md)	 - Fetch metadata of a new YugabyteDB version from a URL


## yba yb-db-version

Manage YugabyteDB versions

### Synopsis

Manage YugabyteDB versions

```
yba yb-db-version [flags]
```

### Options

```
  -h, --help   help for yb-db-version
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba yb-db-version architecture](yba_yb-db-version_architecture.md)	 - Manage architectures for a version of YugabyteDB
* [yba yb-db-version artifact-create](yba_yb-db-version_artifact-create.md)	 - Fetch artifact metadata for a version of YugabyteDB
* [yba yb-db-version create](yba_yb-db-version_create.md)	 - Create a YugabyteDB version entry on YugabyteDB Anywhere
* [yba yb-db-version delete](yba_yb-db-version_delete.md)	 - Delete a YugabyteDB version
* [yba yb-db-version describe](yba_yb-db-version_describe.md)	 - Describe a YugabyteDB version
* [yba yb-db-version list](yba_yb-db-version_list.md)	 - List YugabyteDB versions
* [yba yb-db-version update](yba_yb-db-version_update.md)	 - Update a YugabyteDB version entry on YugabyteDB Anywhere


## yba yb-db-version update

Update a YugabyteDB version entry on YugabyteDB Anywhere

### Synopsis

Update a YugabyteDB version entry on YugabyteDB Anywhere. Run this command after the information provided in the "yba yb-db-version artifact-create <url/upload>" commands.

```
yba yb-db-version update [flags]
```

### Examples

```
yba yb-db-version update --version <version> --state <state>
```

### Options

```
  -v, --version string   [Required] YugabyteDB version to be updated.
      --state string     [Optional] Update the state of the release. Allowed values: active, disabled
      --tag string       [Optional] Update the release tag
      --date-msecs int   [Optional] Update Date in milliseconds since the epoch when the release was created.
      --notes string     [Optional] Update the release notes.
  -h, --help             help for update
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


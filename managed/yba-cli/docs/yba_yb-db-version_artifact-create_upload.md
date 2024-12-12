## yba yb-db-version artifact-create upload

Upload a new YugabyteDB version from a tar gz file

### Synopsis

Upload a new version of YugabyteDB from a tar gz file. Use the output of this command in the "yba yb-db-version create" command.

```
yba yb-db-version artifact-create upload [flags]
```

### Examples

```
yba yb-db-version artifact-create upload --file <file>
```

### Options

```
      --file string   [Required] Tar gz file path of the release
  -h, --help          help for upload
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

* [yba yb-db-version artifact-create](yba_yb-db-version_artifact-create.md)	 - Fetch artifact metadata for a version of YugabyteDB


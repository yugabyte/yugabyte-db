## yba universe file-collection create

Collect files from database nodes in a universe

### Synopsis

Collect specified files from one or more database nodes in a universe. Files are packaged into a tar.gz archive on each node.

```
yba universe file-collection create [flags]
```

### Examples

```
yba universe file-collection create --name <universe-name> \
  --file-paths "/home/yugabyte/tserver/logs/yb-tserver.INFO,/home/yugabyte/master/logs/yb-master.INFO"

yba universe file-collection create --name <universe-name> \
  --file-paths "/home/yugabyte/bin/version_metadata.json" \
  --directory-paths "/home/yugabyte/tserver/logs" --max-depth 2

yba universe file-collection create --name <universe-name> \
  --file-paths "/home/yugabyte/tserver/logs/yb-tserver.INFO" \
  --node-names "yb-1-node-n1,yb-1-node-n2" --masters-only
```

### Options

```
      --file-paths string          [Optional*] Comma-separated list of file paths to collect from each node. Paths can be absolute or relative to the yugabyte home directory. [33mAt least one of file-paths or directory-paths is required.
      --directory-paths string     [Optional*] Comma-separated list of directory paths to collect files from. [33mAt least one of file-paths or directory-paths is required.
      --max-depth int32            [Optional] Maximum depth for directory traversal (1-10). (default 1)
      --max-file-size-bytes int    [Optional] Maximum size of individual files to collect in bytes (default 10MB). (default 10485760)
      --max-total-size-bytes int   [Optional] Maximum total size of all files per node in bytes (default 100MB). (default 104857600)
      --timeout-secs int           [Optional] Timeout in seconds for file collection on each node (default 300). (default 300)
      --linux-user string          [Optional] Linux user to run file collection as. (default "yugabyte")
      --node-names string          [Optional] Comma-separated list of specific node names to collect from.
      --masters-only               [Optional] Collect files only from master nodes. (default false)
      --tservers-only              [Optional] Collect files only from tserver nodes. (default false)
  -h, --help                       help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        The name of the universe for the corresponding file collection operations. Required for create, download, delete.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe file-collection](yba_universe_file-collection.md)	 - Collect, download, and delete diagnostic files from DB nodes


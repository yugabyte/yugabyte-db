## yba universe file-collection delete

Delete collected files from database nodes and/or YBA

### Synopsis

Delete tar archives created by the create file collection command. By default, deletes files from DB nodes. Use --delete-from-yba to also remove downloaded files from YBA local storage.

```
yba universe file-collection delete [flags]
```

### Examples

```
yba universe file-collection delete --name <universe-name> --uuid <collection-uuid>

yba universe file-collection delete --name <universe-name> --uuid <collection-uuid> \
  --delete-from-yba --force
```

### Options

```
  -u, --uuid string            [Required] The collection UUID to delete.
      --delete-from-db-nodes   [Optional] Whether to delete tar files from database nodes. (default true)
      --delete-from-yba        [Optional] Whether to also delete downloaded files from YBA local storage. (default false)
  -h, --help                   help for delete
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


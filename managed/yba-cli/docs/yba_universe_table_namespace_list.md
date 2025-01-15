## yba universe table namespace list

List YugabyteDB Anywhere universe table namespaces

### Synopsis

List YugabyteDB Anywhere universe table namespaces

```
yba universe table namespace list [flags]
```

### Examples

```
yba universe table namespace list --name <universe-name>
```

### Options

```
      --namespace-name string       [Optional] Namespace name to be listed.
      --include-system-namespaces   [Optional] Include system namespaces. (default false)
  -h, --help                        help for list
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe for the corresponding table operations.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe table namespace](yba_universe_table_namespace.md)	 - Manage YugabyteDB Anywhere universe table namespaces


## yba universe table

Manage YugabyteDB Anywhere universe tables

### Synopsis

Manage YugabyteDB Anywhere universe tables

```
yba universe table [flags]
```

### Options

```
  -h, --help          help for table
  -n, --name string   [Required] The name of the universe for the corresponding table operations.
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

* [yba universe](yba_universe.md)	 - Manage YugabyteDB Anywhere universes
* [yba universe table describe](yba_universe_table_describe.md)	 - Describe a YugabyteDB Anywhere universe table
* [yba universe table list](yba_universe_table_list.md)	 - List YugabyteDB Anywhere universe tables
* [yba universe table namespace](yba_universe_table_namespace.md)	 - Manage YugabyteDB Anywhere universe table namespaces
* [yba universe table tablespace](yba_universe_table_tablespace.md)	 - Manage YugabyteDB Anywhere universe tablespaces


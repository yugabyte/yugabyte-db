## yba universe table describe

Describe a YugabyteDB Anywhere universe table

### Synopsis

Describe a universe table in YugabyteDB Anywhere

```
yba universe table describe [flags]
```

### Examples

```
yba universe table describe --name <universe-name> --table-uuid <table-uuid>
```

### Options

```
      --table-uuid string   [Required] The UUID of the table to be described.
  -h, --help                help for describe
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

* [yba universe table](yba_universe_table.md)	 - Manage YugabyteDB Anywhere universe tables


## yba universe table tablespace describe

Describe a YugabyteDB Anywhere universe tablespace

### Synopsis

Describe a universe tablespace in YugabyteDB Anywhere

```
yba universe table tablespace describe [flags]
```

### Examples

```
yba universe table tablespace describe --name <universe-name> \
	--tablespace-name <tablespace-name>
```

### Options

```
      --tablespace-name string   [Required] The Name of the tablespace to be described.
  -h, --help                     help for describe
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

* [yba universe table tablespace](yba_universe_table_tablespace.md)	 - Manage YugabyteDB Anywhere universe tablespaces


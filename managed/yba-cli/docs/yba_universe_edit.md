## yba universe edit

Edit a YugabyteDB Anywhere universe

### Synopsis

Edit a universe in YugabyteDB Anywhere

```
yba universe edit [flags]
```

### Options

```
  -n, --name string        [Required] The name of the universe to be edited.
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
  -h, --help               help for edit
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
* [yba universe edit cluster](yba_universe_edit_cluster.md)	 - Edit clusters in a YugabyteDB Anywhere universe
* [yba universe edit ycql](yba_universe_edit_ycql.md)	 - Edit YCQL settings for a YugabyteDB Anywhere Universe
* [yba universe edit ysql](yba_universe_edit_ysql.md)	 - Edit YSQL settings for a YugabyteDB Anywhere Universe


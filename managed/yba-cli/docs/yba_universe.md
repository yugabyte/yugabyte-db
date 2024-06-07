## yba universe

Manage YugabyteDB Anywhere universes

### Synopsis

Manage YugabyteDB Anywhere universes

```
yba universe [flags]
```

### Options

```
  -h, --help   help for universe
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
* [yba universe create](yba_universe_create.md)	 - Create YugabyteDB Anywhere universe
* [yba universe delete](yba_universe_delete.md)	 - Delete a YugabyteDB Anywhere universe
* [yba universe describe](yba_universe_describe.md)	 - Describe a YugabyteDB Anywhere universe
* [yba universe list](yba_universe_list.md)	 - List YugabyteDB Anywhere universes
* [yba universe node](yba_universe_node.md)	 - Manage YugabyteDB Anywhere universe nodes
* [yba universe restart](yba_universe_restart.md)	 - Restart a YugabyteDB Anywhere Universe
* [yba universe upgrade](yba_universe_upgrade.md)	 - Upgrade a YugabyteDB Anywhere universe


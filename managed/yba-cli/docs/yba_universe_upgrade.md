## yba universe upgrade

Upgrade a YugabyteDB Anywhere universe

### Synopsis

Upgrade a universe in YugabyteDB Anywhere

```
yba universe upgrade [flags]
```

### Options

```
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -n, --name string        [Required] The name of the universe to be ugraded.
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
  -h, --help               help for upgrade
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
* [yba universe upgrade gflags](yba_universe_upgrade_gflags.md)	 - Gflags upgrade for a YugabyteDB Anywhere Universe
* [yba universe upgrade linux-os](yba_universe_upgrade_linux-os.md)	 - VM Linux OS patch for a YugabyteDB Anywhere Universe
* [yba universe upgrade software](yba_universe_upgrade_software.md)	 - Software upgrade for a YugabyteDB Anywhere Universe


## yba universe upgrade gflags

Gflags upgrade for a YugabyteDB Anywhere Universe

### Synopsis

Gflags upgrade for a YugabyteDB Anywhere Universe. Fetch the output of "yba universe upgrade gflags get" command, make required changes to the gflags and submit the json input to "yba universe upgrade gflags set"

```
yba universe upgrade gflags [flags]
```

### Options

```
  -h, --help   help for gflags
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe to be upgraded.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe upgrade](yba_universe_upgrade.md)	 - Upgrade a YugabyteDB Anywhere universe
* [yba universe upgrade gflags get](yba_universe_upgrade_gflags_get.md)	 - Get gflags for a YugabyteDB Anywhere Universe
* [yba universe upgrade gflags set](yba_universe_upgrade_gflags_set.md)	 - Set gflags for a YugabyteDB Anywhere Universe


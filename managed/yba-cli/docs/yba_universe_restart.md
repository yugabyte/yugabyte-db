## yba universe restart

Restart a YugabyteDB Anywhere Universe

### Synopsis

Restart a YugabyteDB Anywhere Universe

```
yba universe restart [flags]
```

### Options

```
  -n, --name string                          [Required] The name of the universe to be restarted.
  -f, --force                                [Optional] Bypass the prompt for non-interactive usage.
  -s, --skip-validations                     [Optional] Skip validations before running the CLI command.
      --delay-between-master-servers int32   [Optional] Upgrade delay between Master servers (in miliseconds). (default 18000)
      --delay-between-tservers int32         [Optional] Upgrade delay between Tservers (in miliseconds). (default 18000)
      --upgrade-option string                [Optional] Upgrade Options, defaults to Rolling. Allowed values (case sensitive): Rolling, Non-Rolling (involves DB downtime). Only a "Rolling" type of restart is allowed on a Kubernetes universe. (default "Rolling")
  -h, --help                                 help for restart
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


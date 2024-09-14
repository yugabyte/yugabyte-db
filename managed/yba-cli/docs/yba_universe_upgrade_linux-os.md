## yba universe upgrade linux-os

VM Linux OS patch for a YugabyteDB Anywhere Universe

### Synopsis

VM Linux OS patch for a YugabyteDB Anywhere Universe. Supported only for universes of cloud type AWS, GCP and Azure.

```
yba universe upgrade linux-os [flags]
```

### Options

```
      --primary-linux-version string         [Required] Primary cluster linux OS version name to be applied.
      --inherit-from-primary                 [Optional] Apply the same linux OS version of primary cluster to read replica cluster. (default true)
      --rr-linux-version string              [Optional] Read replica cluster linux OS version name to be applied. Ignored if inherit-from-primary is set to true.
      --delay-between-master-servers int32   [Optional] Upgrade delay between Master servers (in miliseconds). (default 18000)
      --delay-between-tservers int32         [Optional] Upgrade delay between Tservers (in miliseconds). (default 18000)
  -h, --help                                 help for linux-os
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
  -n, --name string        [Required] The name of the universe to be ugraded.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe upgrade](yba_universe_upgrade.md)	 - Upgrade a YugabyteDB Anywhere universe


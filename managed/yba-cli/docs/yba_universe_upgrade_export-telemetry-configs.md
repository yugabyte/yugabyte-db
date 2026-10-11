## yba universe upgrade export-telemetry-configs

Manage telemetry export configuration for a YugabyteDB Anywhere Universe

### Synopsis

Manage telemetry export configuration for a YugabyteDB Anywhere Universe. Fetch the output of "yba universe upgrade export-telemetry-configs get", make the required changes and submit the json input to "yba universe upgrade export-telemetry-configs set". YugabyteDB Anywhere stores one telemetry configuration per universe and the set command replaces it wholesale, so any export type left out of the input is disabled.

```
yba universe upgrade export-telemetry-configs [flags]
```

### Options

```
  -h, --help   help for export-telemetry-configs
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -f, --force              [Optional] Bypass the prompt for non-interactive usage.
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Required] The name of the universe to be upgraded.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe upgrade](yba_universe_upgrade.md)	 - Upgrade a YugabyteDB Anywhere universe
* [yba universe upgrade export-telemetry-configs get](yba_universe_upgrade_export-telemetry-configs_get.md)	 - Get the telemetry export configuration of a YugabyteDB Anywhere Universe
* [yba universe upgrade export-telemetry-configs set](yba_universe_upgrade_export-telemetry-configs_set.md)	 - Set the telemetry export configuration of a YugabyteDB Anywhere Universe


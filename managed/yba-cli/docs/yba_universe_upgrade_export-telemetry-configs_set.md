## yba universe upgrade export-telemetry-configs set

Set the telemetry export configuration of a YugabyteDB Anywhere Universe

### Synopsis

Set the telemetry export configuration of a YugabyteDB Anywhere Universe. The input replaces the whole configuration, so any export type left out is disabled. Use the output of "yba universe upgrade export-telemetry-configs get" as the starting point, and pass {} to disable every export. Each exporter_uuid accepts either a telemetry provider UUID or its name. Refer to https://github.com/yugabyte/yugabyte-db/blob/master/managed/yba-cli/templates for the structure of the telemetry config file.

```
yba universe upgrade export-telemetry-configs set [flags]
```

### Examples

```
yba universe upgrade export-telemetry-configs set -n <universe-name> \
	--telemetry-config-file-path <file-path>
```

### Options

```
      --telemetry-config string              [Optional] Telemetry export configuration to be set. Use the modified output of "yba universe upgrade export-telemetry-configs get" as the flag value. Quote the string with single quotes. Provide either telemetry-config or telemetry-config-file-path
      --telemetry-config-file-path string    [Optional] Path to the modified json output file of "yba universe upgrade export-telemetry-configs get". Provide either telemetry-config or telemetry-config-file-path
      --upgrade-option string                [Optional] Upgrade option, defaults to Rolling. Allowed values (case insensitive): Rolling, Non-Rolling (involves DB downtime). (default "Rolling")
      --delay-between-master-servers int32   [Optional] Upgrade delay between Master servers (in milliseconds). (default 18000)
      --delay-between-tservers int32         [Optional] Upgrade delay between Tservers (in milliseconds). (default 18000)
      --dry-run                              [Optional] Only validate the input and report what would change.
  -h, --help                                 help for set
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

* [yba universe upgrade export-telemetry-configs](yba_universe_upgrade_export-telemetry-configs.md)	 - Manage telemetry export configuration for a YugabyteDB Anywhere Universe


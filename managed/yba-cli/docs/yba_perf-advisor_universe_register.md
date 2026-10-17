## yba perf-advisor universe register

Register a universe with the YugabyteDB Anywhere Perf Advisor collector

### Synopsis

Register a universe with the Perf Advisor collector.

BASIC collects and stores locally. ADVANCED also remote-writes metrics into YBA's Prometheus. ONLINE forwards everything to the endpoint named by --endpoint-name and keeps nothing locally; the endpoint is pushed to the collector before the universe is registered, so an unreachable destination fails the task.

```
yba perf-advisor universe register [flags]
```

### Examples

```
yba perf-advisor universe register --universe-name <universe> --mode ONLINE \
    --endpoint-name byoc-prod
```

### Options

```
      --universe-name string   [Required] Name of the universe.
      --mode string            [Optional] Collection mode. Allowed values: BASIC, ADVANCED, ONLINE. (default "BASIC")
      --endpoint-name string   [Optional] Perf Advisor endpoint to forward to. Required for ONLINE mode.
  -h, --help                   help for register
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --ca-cert string     CA certificate file path for secure connection to YugabyteDB Anywhere. Required when the endpoint is https and --insecure is not set.
      --config string      Full path to a specific configuration file for YBA CLI. If provided, this takes precedence over the directory specified via --directory, and the generated files are added to the same path. If not provided, the CLI will look for '.yba-cli.yaml' in the directory specified by --directory. Defaults to '$HOME/.yba-cli/.yba-cli.yaml'.
      --debug              Use debug mode, same as --logLevel debug.
      --directory string   Directory containing YBA CLI configuration and generated files. If specified, the CLI will look for a configuration file named '.yba-cli.yaml' in this directory. Defaults to '$HOME/.yba-cli/'.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba perf-advisor universe](yba_perf-advisor_universe.md)	 - Manage YugabyteDB Anywhere Perf Advisor universe registration


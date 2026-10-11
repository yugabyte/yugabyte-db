## yba perf-advisor universe

Manage YugabyteDB Anywhere Perf Advisor universe registration

### Synopsis

Register and unregister universes with the Perf Advisor collector, in one of the three collection modes.

```
yba perf-advisor universe [flags]
```

### Options

```
  -h, --help   help for universe
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

* [yba perf-advisor](yba_perf-advisor.md)	 - Manage YugabyteDB Anywhere Perf Advisor collection
* [yba perf-advisor universe register](yba_perf-advisor_universe_register.md)	 - Register a universe with the YugabyteDB Anywhere Perf Advisor collector
* [yba perf-advisor universe status](yba_perf-advisor_universe_status.md)	 - Show how a universe is registered with Perf Advisor
* [yba perf-advisor universe unregister](yba_perf-advisor_universe_unregister.md)	 - Unregister a universe from the YugabyteDB Anywhere Perf Advisor collector


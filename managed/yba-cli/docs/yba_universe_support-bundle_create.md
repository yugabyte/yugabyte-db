## yba universe support-bundle create

Create a support bundle for a YugabyteDB Anywhere universe

### Synopsis

Create a support bundle for a YugabyteDB Anywhere universe

```
yba universe support-bundle create [flags]
```

### Examples

```
yba universe support-bundle -n dkumar-cli create \
  --components "UniverseLogs, OutputFiles, ErrorFiles, SystemLogs, PrometheusMetrics, ApplicationLogs" \
  --start-time 2025-04-17T00:00:00Z --end-time 2025-04-17T23:59:59Z
```

### Options

```
      --components string               [Required] Comman separated list of components to include in the support bundle. Run "yba universe support-bundle list-components" to check list of case sensitive allowed components.
      --start-time string               [Required] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for start time of the support bundle.
      --end-time string                 [Required] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for end time of the support bundle.
      --max-recent-cores int            [Optional] Maximum number of most recent cores to include in the support bundle if any. (default 1)
      --max-core-file-size int          [Optional] Maximum size in bytes of the recent collected cores if any. (default 25000000000)
      --prom-dump-start-time string     [Optional] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for start time to filter prometheus metrics. Required with --prom-dump-end-time flag. If not provided, the last 15 mins of prometheus metrics from end-time will be collected.
      --prom-dump-end-time string       [Optional] ISO 8601 (YYYY-MM-DDTHH:MM:SSZ) format for end time to filter prometheus metrics. Required with --prom-dump-start-time flag. If not provided, the last 15 mins of prometheus metrics from end-time will be collected.
      --prom-metric-types string        [Optional] Comma separated list of prometheus metric types to include in the support bundle. Allowed values: master_export, node_export, platform, prometheus, tserver_export, cql_export, ysql_export
      --prom-queries string             [Optional] Prometheus queries in JSON map format to include in the support bundle. Provide the prometheus queries in the following format: "--prom-queries '{"key-1":"value-1","key-2":"value-2"}'" where the key is the name of the custom query and the value is the query. Provide either prom-queries or prom-queries-file-path
      --prom-queries-file-path string   [Optional] Path to a file containing prometheus queries in JSON map format to include in the support bundle. Provide either prom-queries or prom-queries-file-path
  -h, --help                            help for create
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
  -n, --name string        [Optional] The name of the universe for support bundle operations. Required for create, delete, list, describe, download.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
  -s, --skip-validations   [Optional] Skip validations before running the CLI command.
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba universe support-bundle](yba_universe_support-bundle.md)	 - Support bundle operations on a YugabyteDB Anywhere universe


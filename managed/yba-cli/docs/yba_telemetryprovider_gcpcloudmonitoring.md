## yba telemetryprovider gcpcloudmonitoring

Manage a YugabyteDB Anywhere GCP Cloud Monitoring telemetry provider

### Synopsis

Manage a GCP Cloud Monitoring telemetry provider in YugabyteDB Anywhere

```
yba telemetryprovider gcpcloudmonitoring [flags]
```

### Options

```
  -h, --help          help for gcpcloudmonitoring
  -n, --name string   [Optional] The name of the provider for the action. Required for create, delete, describe.
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

* [yba telemetryprovider](yba_telemetryprovider.md)	 - Manage YugabyteDB Anywhere telemetry providers
* [yba telemetryprovider gcpcloudmonitoring delete](yba_telemetryprovider_gcpcloudmonitoring_delete.md)	 - Delete a GCP Cloud Monitoring YugabyteDB Anywhere telemetry provider
* [yba telemetryprovider gcpcloudmonitoring describe](yba_telemetryprovider_gcpcloudmonitoring_describe.md)	 - Describe a GCP Cloud Monitoring YugabyteDB Anywhere telemetry provider
* [yba telemetryprovider gcpcloudmonitoring list](yba_telemetryprovider_gcpcloudmonitoring_list.md)	 - List GCP Cloud Monitoring YugabyteDB Anywhere telemetry providers


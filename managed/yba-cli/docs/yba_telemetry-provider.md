## yba telemetry-provider

Manage YugabyteDB Anywhere telemetry providers

### Synopsis

Manage YugabyteDB Anywhere telemetry providers for exporting logs and metrics via OTEL

```
yba telemetry-provider [flags]
```

### Options

```
  -h, --help   help for telemetry-provider
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

* [yba](yba.md)	 - yba - Command line tools to manage your YugabyteDB Anywhere (Self-managed Database-as-a-Service) resources.
* [yba telemetry-provider awscloudwatch](yba_telemetry-provider_awscloudwatch.md)	 - Manage a YugabyteDB Anywhere AWS CloudWatch telemetry provider
* [yba telemetry-provider datadog](yba_telemetry-provider_datadog.md)	 - Manage a YugabyteDB Anywhere DataDog telemetry provider
* [yba telemetry-provider delete](yba_telemetry-provider_delete.md)	 - Delete a YugabyteDB Anywhere telemetry provider
* [yba telemetry-provider describe](yba_telemetry-provider_describe.md)	 - Describe a YugabyteDB Anywhere telemetry provider
* [yba telemetry-provider gcpcloudmonitoring](yba_telemetry-provider_gcpcloudmonitoring.md)	 - Manage a YugabyteDB Anywhere GCP Cloud Monitoring telemetry provider
* [yba telemetry-provider list](yba_telemetry-provider_list.md)	 - List YugabyteDB Anywhere telemetry providers
* [yba telemetry-provider loki](yba_telemetry-provider_loki.md)	 - Manage a YugabyteDB Anywhere Loki telemetry provider
* [yba telemetry-provider splunk](yba_telemetry-provider_splunk.md)	 - Manage a YugabyteDB Anywhere Splunk telemetry provider


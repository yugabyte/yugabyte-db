## yba telemetry-provider datadog

Manage a YugabyteDB Anywhere DataDog telemetry provider

### Synopsis

Manage a DataDog telemetry provider in YugabyteDB Anywhere

```
yba telemetry-provider datadog [flags]
```

### Options

```
  -h, --help          help for datadog
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

* [yba telemetry-provider](yba_telemetry-provider.md)	 - Manage YugabyteDB Anywhere telemetry providers
* [yba telemetry-provider datadog create](yba_telemetry-provider_datadog_create.md)	 - Create a YugabyteDB Anywhere DataDog telemetry provider
* [yba telemetry-provider datadog delete](yba_telemetry-provider_datadog_delete.md)	 - Delete a DataDog YugabyteDB Anywhere telemetry provider
* [yba telemetry-provider datadog describe](yba_telemetry-provider_datadog_describe.md)	 - Describe a YugabyteDB Anywhere DataDog telemetry provider
* [yba telemetry-provider datadog list](yba_telemetry-provider_datadog_list.md)	 - List DataDog YugabyteDB Anywhere telemetry providers


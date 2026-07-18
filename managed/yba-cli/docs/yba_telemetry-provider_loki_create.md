## yba telemetry-provider loki create

Create a YugabyteDB Anywhere Loki telemetry provider

### Synopsis

Create a Loki telemetry provider in YugabyteDB Anywhere

```
yba telemetry-provider loki create [flags]
```

### Examples

```
yba telemetryprovider loki create --name <name> \
     --endpoint <loki-endpoint> --auth-type <auth-type>
```

### Options

```
      --endpoint string          [Required] Loki endpoint URL. Provide the endpoint in the following format: http://<loki-url>:<loki-port>. Ensure loki is accessible through the mentioned port. YugbayteDB Anywhere will push logs to <endpoint>/loki/api/v1/push.
      --organization-id string   [Optional] Organization/Tenant ID is required when multi-tenancy is set up in Loki. Optional for Grafana Cloud since the authentication reroutes requests according to scope.
      --auth-type string         [Optional] Authentication type to be used for Loki telemetry provider. Allowed values: none, basic. (default "none")
      --username string          [Optional] Username for basic authentication. Required if auth-type is set to basic.
      --password string          [Optional] Password for basic authentication. Required if auth-type is set to basic.
      --tags stringToString      [Optional] Tags to be applied to the exporter config. Provide as key-value pairs per flag. Example "--tags name=test --tags owner=development" OR "--tags name=test,owner=development". (default [])
  -h, --help                     help for create
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
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba telemetry-provider loki](yba_telemetry-provider_loki.md)	 - Manage a YugabyteDB Anywhere Loki telemetry provider


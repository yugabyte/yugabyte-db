## yba telemetry-provider otlp create

Create a YugabyteDB Anywhere OTLP telemetry provider

### Synopsis

Create an OTLP telemetry provider in YugabyteDB Anywhere. Requires the global runtime configuration "yb.telemetry.allow_otlp" to be true.

```
yba telemetry-provider otlp create [flags]
```

### Examples

```
yba telemetry-provider otlp create --name <name> \
     --endpoint <endpoint> --auth-type basic --username <username> --password <password>
```

### Options

```
      --endpoint string                 [Required] OTLP collector endpoint. For HTTP protocol log export, "/v1/logs" is appended.
      --protocol string                 [Optional] OTLP protocol, defaults to gRPC. Allowed values (case insensitive): gRPC, HTTP. (default "gRPC")
      --auth-type string                [Optional] OTLP authentication type, defaults to none. Allowed values (case insensitive): none, basic, bearer-token. (default "none")
      --username string                 [Optional] Username. Required with password for basic authentication.
      --password string                 [Optional] Password. Required with username for basic authentication.
      --bearer-token string             [Optional] Bearer token. Required for bearer-token authentication.
      --compression string              [Optional] Compression for exported data, defaults to gzip. Allowed values (case insensitive): gzip, none, snappy, zstd.
      --timeout-seconds int32           [Optional] Export request timeout in seconds. (default 5)
      --logs-endpoint string            [Optional] Target URL for logs, overriding endpoint. HTTP protocol only.
      --metrics-endpoint string         [Optional] Target URL for metrics, overriding endpoint. HTTP protocol only.
      --headers stringToString          [Optional] Headers to send with each export request. Provide as key-value pairs per flag. Example "--headers X-Scope-OrgID=tenant1 --headers X-Custom=value". (default [])
      --retry-enabled                   [Optional] Enable exporter retry on failure.
      --retry-initial-interval string   [Optional] Initial retry interval as a duration, for example "5s", "1m".
      --retry-max-interval string       [Optional] Maximum retry interval as a duration, for example "30s", "1m".
      --retry-max-elapsed-time string   [Optional] Maximum total retry time as a duration, for example "5m", "60m".
      --tags stringToString             [Optional] Tags to be applied to the exporter config. Provide as key-value pairs per flag. Example "--tags name=test --tags owner=development" OR "--tags name=test,owner=development". (default [])
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
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
      --insecure           Allow insecure connections to YugabyteDB Anywhere. Value ignored for http endpoints. Defaults to false for https.
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba telemetry-provider otlp](yba_telemetry-provider_otlp.md)	 - Manage a YugabyteDB Anywhere OTLP telemetry provider


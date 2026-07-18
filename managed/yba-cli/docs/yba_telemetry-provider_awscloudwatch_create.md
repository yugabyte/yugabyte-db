## yba telemetry-provider awscloudwatch create

Create a YugabyteDB Anywhere AWS CloudWatch telemetry provider

### Synopsis

Create a AWS CloudWatch telemetry provider in YugabyteDB Anywhere

```
yba telemetry-provider awscloudwatch create [flags]
```

### Examples

```
yba telemetryprovider awscloudwatch create --name <name> \
    --access-key-id <access-key-id> --secret-access-key <secret-access-key> --region <region> \
    --log-group <log-group> --log-stream <log-stream>
```

### Options

```
      --access-key-id string       AWS Access Key ID. Can also be set using environment variable AWS_ACCESS_KEY_ID.
      --secret-access-key string   AWS Secret Access Key. Can also be set using environment variable AWS_SECRET_ACCESS_KEY.
      --region string              AWS region. Can also be set using environment variable AWS_REGION
      --log-group string           [Required] AWS CloudWatch Log Group.
      --log-stream string          [Required] AWS CloudWatch Log Stream.
      --role-arn string            [Optional] AWS Role ARN.
      --endpoint string            [Optional] AWS Endpoint.
      --tags stringToString        [Optional] Tags to be applied to the exporter config. Provide as key-value pairs per flag. Example "--tags name=test --tags owner=development" OR "--tags name=test,owner=development". (default [])
  -h, --help                       help for create
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

* [yba telemetry-provider awscloudwatch](yba_telemetry-provider_awscloudwatch.md)	 - Manage a YugabyteDB Anywhere AWS CloudWatch telemetry provider


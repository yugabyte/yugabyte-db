## yba telemetry-provider s3 create

Create a YugabyteDB Anywhere S3 telemetry provider

### Synopsis

Create an S3 telemetry provider in YugabyteDB Anywhere

```
yba telemetry-provider s3 create [flags]
```

### Examples

```
yba telemetry-provider s3 create --name <name> \
     --bucket <bucket> --region <region>
```

### Options

```
      --bucket string                         [Required] S3 bucket name.
      --region string                         [Optional] S3 bucket region. Can also be set using the environment variable AWS_REGION.
      --access-key-id string                  [Optional] AWS Access Key ID. Required with secret-access-key, or set both using environment variables AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY.
      --secret-access-key string              [Optional] AWS Secret Access Key. Required with access-key-id, or set both using environment variables AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY.
      --role-arn string                       [Optional] AWS IAM role ARN to assume when writing to the bucket.
      --directory-prefix string               [Optional] Root directory inside the bucket. Defaults to "yb-logs/".
      --file-prefix string                    [Optional] Prefix for exported object names. Defaults to "yb-otel-".
      --partition string                      [Optional] Partition granularity for the object key layout, defaults to minute. Allowed values (case insensitive): hour, minute.
      --marshaler string                      [Optional] Encoding of exported objects, defaults to OTLP_JSON. Allowed values (case insensitive): OTLP_JSON, SUMO_IC. SUMO_IC is allowed for logs only.
      --endpoint string                       [Optional] Override the endpoint instead of deriving it from region and bucket. Use for S3 compatible object stores.
      --disable-ssl                           [Optional] Disable SSL when connecting to the endpoint.
      --force-path-style                      [Optional] Force path style addressing instead of virtual hosted style.
      --include-universe-and-node-in-prefix   [Optional] Append universe UUID and node name to the directory prefix.
      --tags stringToString                   [Optional] Tags to be applied to the exporter config. Provide as key-value pairs per flag. Example "--tags name=test --tags owner=development" OR "--tags name=test,owner=development". (default [])
  -h, --help                                  help for create
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

* [yba telemetry-provider s3](yba_telemetry-provider_s3.md)	 - Manage a YugabyteDB Anywhere S3 telemetry provider


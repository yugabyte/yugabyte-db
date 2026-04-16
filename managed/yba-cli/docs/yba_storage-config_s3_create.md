## yba storage-config s3 create

Create an S3 YugabyteDB Anywhere storage configuration

### Synopsis

Create an S3 storage configuration in YugabyteDB Anywhere

```
yba storage-config s3 create [flags]
```

### Examples

```
yba storage-config s3 create --name <storage-configuration-name> \
	--backup-location <backup-location> --access-key <access-key> --secret-key <secret-key>
```

### Options

```
      --backup-location string     [Required] The complete backup location including "s3://".
      --access-key-id string       S3 Access Key ID. Required for non IAM role based storage configurations. Can also be set using environment variable AWS_ACCESS_KEY_ID.
      --secret-access-key string   S3 Secret Access Key. Required for non IAM role based storage configurations. Can also be set using environment variable AWS_SECRET_ACCESS_KEY.
      --use-iam-instance-profile   [Optional] Use IAM Role from the YugabyteDB Anywhere Host. Configuration creation will fail on insufficient permissions on the host. (default false)
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
  -n, --name string        [Optional] The name of the storage configuration for the operation. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba storage-config s3](yba_storage-config_s3.md)	 - Manage a YugabyteDB Anywhere S3 storage configuration


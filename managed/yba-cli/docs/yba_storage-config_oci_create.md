## yba storage-config oci create

Create an OCI YugabyteDB Anywhere storage configuration

### Synopsis

Create an OCI Object Storage configuration in YugabyteDB Anywhere. Authenticate either with OCI IAM (--use-oci-iam with --namespace) or with OCI S3-compatible Customer Secret Keys and --s3-host-base. The key pair can be passed with --access-key-id and --secret-access-key, or read from the OCI_S3_ACCESS_KEY_ID and OCI_S3_SECRET_ACCESS_KEY environment variables.

```
yba storage-config oci create [flags]
```

### Examples

```
yba storage-config oci create --name <storage-configuration-name> \
	--backup-location s3://<bucket>/<prefix> --region <oci-region> \
	--s3-host-base <namespace>.compat.objectstorage.<oci-region>.oraclecloud.com

yba storage-config oci create --name <storage-configuration-name> \
	--backup-location https://objectstorage.<oci-region>.oraclecloud.com/n/<namespace>/b/<bucket> \
	--region <oci-region> --use-oci-iam --namespace <namespace>
```

### Options

```
      --backup-location string     [Required] The complete backup location. Accepted formats: s3://<bucket>[/<path>] or https://objectstorage.<region>.oraclecloud.com/n/<namespace>/b/<bucket>[/<path>].
      --region string              [Required] OCI region for Object Storage, e.g. us-sanjose-1.
      --use-oci-iam                [Optional] Use OCI IAM for native Object Storage access from the YugabyteDB Anywhere host. Configuration creation will fail on insufficient permissions on the host. (default false)
      --namespace string           OCI Object Storage namespace. Required when --use-oci-iam is set. When using a native HTTPS backup location, it must match the /n/<namespace>/ segment of the URL.
      --access-key-id string       OCI Customer Secret Key access key for the S3-compatible API. Required for non IAM based storage configurations. Can also be set using environment variable OCI_S3_ACCESS_KEY_ID.
      --secret-access-key string   OCI Customer Secret Key secret for the S3-compatible API. Required for non IAM based storage configurations. Can also be set using environment variable OCI_S3_SECRET_ACCESS_KEY.
      --s3-host-base string        OCI S3-compatible API host base, e.g. <namespace>.compat.objectstorage.<region>.oraclecloud.com. Required for non IAM based storage configurations.
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

* [yba storage-config oci](yba_storage-config_oci.md)	 - Manage a YugabyteDB Anywhere OCI storage configuration


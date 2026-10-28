## yba storage-config oci update

Update an OCI YugabyteDB Anywhere storage configuration

### Synopsis

Update an OCI storage configuration in YugabyteDB Anywhere

```
yba storage-config oci update [flags]
```

### Examples

```
yba storage-config oci update --name <storage-configuration-name> \
	--new-name <new-storage-configuration-name>

yba storage-config oci update --name <storage-configuration-name> \
	--use-oci-iam=false --s3-host-base <namespace>.compat.objectstorage.<region>.oraclecloud.com
```

### Options

```
      --region string              [Optional] Update OCI region for Object Storage, e.g. us-sanjose-1.
      --use-oci-iam                [Optional] Switch between OCI IAM and S3-compatible credentials. Switching to S3-compatible credentials requires --s3-host-base and the key pair, from --access-key-id and --secret-access-key or from OCI_S3_ACCESS_KEY_ID and OCI_S3_SECRET_ACCESS_KEY. Switching to OCI IAM requires a namespace.
      --namespace string           [Optional] Update OCI Object Storage namespace. Required for OCI IAM based storage configurations.
      --access-key-id string       [Optional] Update OCI Customer Secret Key access key. Provide with --secret-access-key for non IAM based storage configurations. Can also be set using environment variable OCI_S3_ACCESS_KEY_ID when switching off OCI IAM.
      --secret-access-key string   [Optional] Update OCI Customer Secret Key secret. Provide with --access-key-id for non IAM based storage configurations. Can also be set using environment variable OCI_S3_SECRET_ACCESS_KEY when switching off OCI IAM.
      --s3-host-base string        [Optional] Update OCI S3-compatible API host base, e.g. <namespace>.compat.objectstorage.<region>.oraclecloud.com.
      --new-name string            [Optional] Update name of the storage configuration.
  -h, --help                       help for update
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


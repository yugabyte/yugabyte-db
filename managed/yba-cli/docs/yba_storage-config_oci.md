## yba storage-config oci

Manage a YugabyteDB Anywhere OCI storage configuration

### Synopsis

Manage an OCI Object Storage configuration in YugabyteDB Anywhere

```
yba storage-config oci [flags]
```

### Options

```
  -n, --name string   [Optional] The name of the storage configuration for the operation. Required for create, delete, describe, update.
  -h, --help          help for oci
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

* [yba storage-config](yba_storage-config.md)	 - Manage YugabyteDB Anywhere storage configurations
* [yba storage-config oci create](yba_storage-config_oci_create.md)	 - Create an OCI YugabyteDB Anywhere storage configuration
* [yba storage-config oci delete](yba_storage-config_oci_delete.md)	 - Delete an OCI YugabyteDB Anywhere storage configuration
* [yba storage-config oci describe](yba_storage-config_oci_describe.md)	 - Describe an OCI YugabyteDB Anywhere storage configuration
* [yba storage-config oci list](yba_storage-config_oci_list.md)	 - List OCI YugabyteDB Anywhere storage-configurations
* [yba storage-config oci update](yba_storage-config_oci_update.md)	 - Update an OCI YugabyteDB Anywhere storage configuration


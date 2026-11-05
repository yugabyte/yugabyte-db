## yba ear oci update

Update a YugabyteDB Anywhere OCI encryption at rest (EAR) configuration

### Synopsis

Update an OCI encryption at rest (EAR) configuration in YugabyteDB Anywhere. API key credentials and compartment OCID can be updated. Auth type, vault, region, and key name cannot be changed.

```
yba ear oci update [flags]
```

### Examples

```
yba ear oci update --name <config-name> \
    --tenancy-id <tenancy-ocid> --user-id <user-ocid> --fingerprint <fingerprint> \
    --private-key-file-path <path-to-pem>

yba ear oci update --name <config-name> --compartment-id <compartment-ocid>
```

### Options

```
      --tenancy-id string              [Optional] Update OCI tenancy OCID. Must be set with --user-id, --fingerprint, and --private-key-file-path.
      --user-id string                 [Optional] Update OCI user OCID. Must be set with --tenancy-id, --fingerprint, and --private-key-file-path.
      --fingerprint string             [Optional] Update API signing key fingerprint. Must be set with --tenancy-id, --user-id, and --private-key-file-path.
      --private-key-file-path string   [Optional] Path to the updated API private key PEM file. Must be set with --tenancy-id, --user-id, and --fingerprint.
      --compartment-id string          [Optional] Update OCI compartment OCID.
  -h, --help                           help for update
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
  -n, --name string        [Optional] The name of the configuration for the action. Required for create, delete, describe, update and refresh.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba ear oci](yba_ear_oci.md)	 - Manage a YugabyteDB Anywhere OCI encryption at rest (EAR) configuration


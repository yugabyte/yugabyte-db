## yba ear oci create

Create a YugabyteDB Anywhere OCI encryption at rest configuration

### Synopsis

Create an OCI encryption at rest configuration in YugabyteDB Anywhere. For API_KEY auth, pass --tenancy-id, --user-id, --fingerprint, and --private-key-file-path, or set the corresponding OCI_* environment variables. Ensure yb.kms.allow_oci is set to true in the YugabyteDB Anywhere configuration file.

```
yba ear oci create [flags]
```

### Examples

```
yba ear oci create --name <config-name> \
    --vault-id <vault-ocid> --region <region> --compartment-id <compartment-ocid> \
    --key-name <key-name> --auth-type API_KEY \
    --tenancy-id <tenancy-ocid> --user-id <user-ocid> --fingerprint <fingerprint> \
    --private-key-file-path <path-to-pem>
```

### Options

```
      --auth-type string               [Optional] OCI authentication type. Allowed values: API_KEY, INSTANCE_PRINCIPAL. (default "API_KEY")
      --vault-id string                [Required] OCI vault OCID.
      --region string                  OCI region identifier, for example us-ashburn-1. Can also be set using environment variable OCI_REGION.
      --compartment-id string          [Required] OCI compartment OCID.
      --key-name string                [Required] Display name of the AES key in the vault. An existing enabled AES key with this name is used. Otherwise a new key is created.
      --tenancy-id string              OCI tenancy OCID. Required for API_KEY auth-type. Can also be set using environment variable OCI_TENANCY.
      --user-id string                 OCI user OCID. Required for API_KEY auth-type. Can also be set using environment variable OCI_USER.
      --fingerprint string             Fingerprint of the API signing key. Required for API_KEY auth-type. Can also be set using environment variable OCI_FINGERPRINT.
      --private-key-file-path string   Path to the API private key PEM file. Required for API_KEY auth-type. Can also be set using environment variable OCI_PRIVATE_KEY_FILE.
  -h, --help                           help for create
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


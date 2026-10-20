## yba provider oci create

Create an OCI YugabyteDB Anywhere provider

### Synopsis

Create an OCI provider in YugabyteDB Anywhere

```
yba provider oci create [flags]
```

### Examples

```
yba provider oci create -n <provider-name> \
	--compartment-id <compartment-ocid> \
	--oci-region us-ashburn-1 \
	--tenancy-id <tenancy-ocid> \
	--user-id <user-ocid> \
	--fingerprint <fingerprint> \
	--private-key-file-path <path-to-api-key.pem> \
	--region region-name=us-ashburn-1::vnet=<vcn-ocid> \
	--zone zone-name=AD-1::region-name=us-ashburn-1::subnet=<subnet-ocid>
```

### Options

```
      --auth-type string                      [Optional] OCI authentication type. Allowed values: API_KEY, INSTANCE_PRINCIPAL. Can also be set using environment variable OCI_AUTH_TYPE. Defaults to API_KEY.
      --tenancy-id string                     OCI Tenancy OCID. Required for API_KEY authentication. Can also be set using environment variable OCI_TENANCY.
      --user-id string                        OCI User OCID. Required for API_KEY authentication. Can also be set using environment variable OCI_USER.
      --fingerprint string                    OCI API key fingerprint. Required for API_KEY authentication. Can also be set using environment variable OCI_FINGERPRINT.
      --private-key-file-path string          Path to the OCI API private key PEM file. Required for API_KEY authentication. Can also be set using environment variable OCI_PRIVATE_KEY_FILE.
      --compartment-id string                 [Required] OCI Compartment OCID. Can also be set using environment variable OCI_COMPARTMENT_ID.
      --oci-region string                     [Required] Default OCI region (for example, us-ashburn-1). Can also be set using environment variable OCI_REGION.
      --hosted-zone-id string                 [Optional] OCI DNS Zone OCID. Can also be set using environment variable OCI_HOSTED_ZONE_ID.
      --region stringArray                    [Required] Region associated with the OCI provider. Minimum number of required regions = 1. Provide the following double colon (::) separated fields as key-value pairs: "region-name=<region-name>::vnet=<vcn-ocid>::instance-template=<instance-config-ocid>". Region name and Virtual network are required key-values. Instance template is the OCID of an OCI instance configuration and is optional. Each region needs to be added using a separate --region flag. Example: --region region-name=us-ashburn-1::vnet=<vcn-ocid>
      --zone stringArray                      [Required] Zone associated to the OCI Region defined. Provide the following double colon (::) separated fields as key-value pairs: "zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-ocid>".Zone name, Region name and subnet IDs are required values. Each --region definition must have atleast one corresponding --zone definition. Multiple --zone definitions can be provided per region.Each zone needs to be added using a separate --zone flag. Example: --zone zone-name=AD-1::region-name=us-ashburn-1::subnet=<subnet-ocid>
      --image-bundle stringArray              [Optional] Image bundles associated with OCI provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-name=<image-bundle-name>::arch=<architecture>::machine-image=<image-ocid>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>". Image bundle name, machine image and SSH user are required key-value pairs. Allowed values for architecture are x86_64 and aarch64, defaults to x86_64. The default SSH user is opc and the default SSH Port is 22. Default marks the image bundle as default for the provider. Each image bundle can be added using separate --image-bundle flag. Example: --image-bundle image-bundle-name=<image-bundle>::machine-image=<image-ocid>::ssh-user=opc::ssh-port=22
      --custom-ssh-keypair-name string        [Optional] Provide custom key pair name to access YugabyteDB nodes. If left empty, YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.
      --custom-ssh-keypair-file-path string   [Optional] Provide custom key pair file path to access YugabyteDB nodes. Required with --custom-ssh-keypair-name.
      --airgap-install                        [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads. (default false)
      --ntp-servers stringArray               [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
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
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, update, and some instance-type subcommands.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider oci](yba_provider_oci.md)	 - Manage a YugabyteDB Anywhere OCI provider


## yba provider oci update

Update an OCI YugabyteDB Anywhere provider

### Synopsis

Update an OCI provider in YugabyteDB Anywhere

```
yba provider oci update [flags]
```

### Examples

```
yba provider oci update --name <provider-name> \
	 --hosted-zone-id <hosted-zone-id>
```

### Options

```
      --new-name string                   [Optional] Updating provider name.
      --auth-type string                  [Optional] Update OCI authentication type. Allowed values: API_KEY, INSTANCE_PRINCIPAL. Switching to API_KEY requires the API key flags. Switching to INSTANCE_PRINCIPAL clears the stored API key credentials.
      --tenancy-id string                 [Optional] Update OCI Tenancy OCID. Must be set with --user-id, --fingerprint and --private-key-file-path.
      --user-id string                    [Optional] Update OCI User OCID. Must be set with --tenancy-id, --fingerprint and --private-key-file-path.
      --fingerprint string                [Optional] Update OCI API key fingerprint. Must be set with --tenancy-id, --user-id and --private-key-file-path.
      --private-key-file-path string      [Optional] Update OCI API private key from a PEM file. Must be set with --tenancy-id, --user-id and --fingerprint.
      --compartment-id string             [Optional] Update OCI Compartment OCID.
      --oci-region string                 [Optional] Update the default OCI region.
      --hosted-zone-id string             [Optional] Update OCI DNS Zone OCID.
      --add-region stringArray            [Optional] Add region associated with the OCI provider. Provide the following double colon (::) separated fields as key-value pairs: "region-name=<region-name>::vnet=<vcn-ocid>::instance-template=<instance-config-ocid>". Region name and Virtual network are required key-values. Instance template is optional. Each region needs to be added using a separate --add-region flag.
      --add-zone stringArray              [Optional] Zone associated to the OCI Region defined. Provide the following double colon (::) separated fields as key-value pairs: "zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-ocid>".Zone name, Region name and subnet IDs are required values. Each --add-region definition must have atleast one corresponding --add-zone definition. Multiple --add-zone definitions can be provided per region.Each zone needs to be added using a separate --add-zone flag.
      --remove-region stringArray         [Optional] Region name to be removed from the provider. Each region to be removed needs to be provided using a separate --remove-region definition. Removing a region removes the corresponding zones.
      --remove-zone stringArray           [Optional] Remove zone associated to the OCI Region defined. Provide the following double colon (::) separated fields as key-value pairs: "zone-name=<zone-name>::region-name=<region-name>". Zone name, Region name are required values. Each zone needs to be removed using a separate --remove-zone flag.
      --edit-region stringArray           [Optional] Edit region details associated with the OCI provider. Provide the following double colon (::) separated fields as key-value pairs: "region-name=<region-name>::vnet=<vcn-ocid>::instance-template=<instance-config-ocid>". Region name is a required key-value pair. Virtual network and instance template are optional. Each region needs to be modified using a separate --edit-region flag.
      --edit-zone stringArray             [Optional] Edit zone associated to the OCI Region defined. Provide the following double colon (::) separated fields as key-value pairs: "zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-ocid>". Zone name, Region name are required values. Subnet ID is optional. Each zone needs to be modified using a separate --edit-zone flag.
      --add-image-bundle stringArray      [Optional] Add image bundles associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-name=<image-bundle-name>::arch=<architecture>::machine-image=<image-ocid>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>". Image bundle name, machine image and SSH user are required key-value pairs. Allowed values for architecture are x86_64 and aarch64, defaults to x86_64. The default SSH Port is 22. Default marks the image bundle as default for the provider. If default is not specified, the bundle will automatically be set as default if no other default bundle exists for the same architecture. Each image bundle can be added using separate --add-image-bundle flag.
      --edit-image-bundle stringArray     [Optional] Edit image bundles associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-uuid=<image-bundle-uuid>::machine-image=<image-ocid>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>". Image bundle UUID is a required key-value pair.Each image bundle can be edited using separate --edit-image-bundle flag.
      --remove-image-bundle stringArray   [Optional] Image bundle UUID to be removed from the provider. Each bundle to be removed needs to be provided using a separate --remove-image-bundle definition.
      --airgap-install                    [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads. (default false)
      --ntp-servers stringArray           [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
  -h, --help                              help for update
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


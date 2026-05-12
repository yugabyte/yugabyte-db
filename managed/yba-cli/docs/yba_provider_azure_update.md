## yba provider azure update

Update an Azure YugabyteDB Anywhere provider

### Synopsis

Update an Azure provider in YugabyteDB Anywhere

```
yba provider azure update [flags]
```

### Examples

```
yba provider azure update --name <provider-name> \
	 --hosted-zone-id <hosted-zone-id>
```

### Options

```
      --new-name string                   [Optional] Updating provider name.
      --client-id string                  [Optional] Update Azure Client ID. Required with client-secret, tenant-id, subscription-id, rg
      --client-secret string              [Optional] Update Azure Client Secret. Required with client-id, tenant-id, subscription-id, rg
      --tenant-id string                  [Optional] Update Azure Tenant ID. Required with client-secret, client-id, subscription-id, rg
      --subscription-id string            [Optional] Update Azure Subscription ID. Required with client-id, client-secret, tenant-id, rg
      --rg string                         [Optional] Update Azure Resource Group. Required with client-id, client-secret, tenant-id, subscription-id
      --network-subscription-id string    [Optional] Update Azure Network Subscription ID.
      --network-rg string                 [Optional] Update Azure Network Resource Group.
      --hosted-zone-id string             [Optional] Update Hosted Zone ID corresponding to Private DNS Zone.
      --add-region stringArray            [Optional] Add region associated with the Azure provider. Provide the following double colon (::) separated fields as key-value pairs: "region-name=<region-name>::vnet=<virtual-network>::sg-id=<security-group-id>". Region name and Virtual network are required key-values. Security Group ID is optional. Each region needs to be added using a separate --add-region flag.
      --add-zone stringArray              [Optional] Zone associated to the Azure Region defined. Provide the following double colon (::) separated fields as key-value pairs: "zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>".Zone name, Region name and subnet IDs are required values. Secondary subnet ID is optional. Each --add-region definition must have atleast one corresponding --add-zone definition. Multiple --add-zone definitions can be provided per region.Each zone needs to be added using a separate --add-zone flag.
      --remove-region stringArray         [Optional] Region name to be removed from the provider. Each region to be removed needs to be provided using a separate --remove-region definition. Removing a region removes the corresponding zones.
      --remove-zone stringArray           [Optional] Remove zone associated to the Azure Region defined. Provide the following double colon (::) separated fields as key-value pairs: "zone-name=<zone-name>::region-name=<region-name>". Zone name, Region name are required values. Each zone needs to be removed using a separate --remove-zone flag.
      --edit-region stringArray           [Optional] Edit region details associated with the Azure provider. Provide the following double colon (::) separated fields as key-value pairs: "region-name=<region-name>::vnet=<virtual-network>::sg-id=<security-group-id>". Region name is a required key-value pair. Virtual network and Security Group ID are optional. Each region needs to be modified using a separate --edit-region flag.
      --edit-zone stringArray             [Optional] Edit zone associated to the Azure Region defined. Provide the following double colon (::) separated fields as key-value pairs: "zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>::secondary-subnet=<secondary-subnet-id>". Zone name, Region name are required values. Subnet IDs and Secondary subnet ID is optional. Each zone needs to be modified using a separate --edit-zone flag.
      --add-image-bundle stringArray      [Optional] Add Intel x86_64 image bundles associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-name=<image-bundle-name>::machine-image=<custom-ami>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>". Image bundle name, machine image and SSH user are required key-value pairs. The default SSH Port is 22. Default marks the image bundle as default for the provider. If default is not specified, the bundle will automatically be set as default if no other default bundle exists for the same architecture. Each image bundle can be added using separate --add-image-bundle flag.
      --edit-image-bundle stringArray     [Optional] Edit Intel x86_64 image bundles associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-uuid=<image-bundle-uuid>::machine-image=<custom-ami>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::default=<true/false>". Image bundle UUID is a required key-value pair.Each image bundle can be added using separate --edit-image-bundle flag.
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

* [yba provider azure](yba_provider_azure.md)	 - Manage a YugabyteDB Anywhere Azure provider


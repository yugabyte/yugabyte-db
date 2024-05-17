## yba provider azure create

Create an Azure YugabyteDB Anywhere provider

### Synopsis

Create an Azure provider in YugabyteDB Anywhere

```
yba provider azure create [flags]
```

### Examples

```
./yba provider azure create -n <provider-name> \
	--region region-name=westus2,vnet=<vnet> --zone zone-name=westus2-1,region-name=westus2,subnet=<subnet> \
	--rg=<az-resource-group> \
	--client-id=<az-client-id> \
	--tenant-id=<az-tenant-id> \
	--client-secret=<az-client-secret> \
	--subscription-id=<az-subscription-id>
```

### Options

```
      --client-id string                      Azure Client ID. Can also be set using environment variable AZURE_CLIENT_ID.
      --client-secret string                  Azure Client Secret. Can also be set using environment variable AZURE_CLIENT_SECRET.
      --tenant-id string                      Azure Tenant ID. Can also be set using environment variable AZURE_TENANT_ID.
      --subscription-id string                Azure Subscription ID. Can also be set using environment variable AZURE_SUBSCRIPTION_ID.
      --rg string                             Azure Resource Group. Can also be set using environment variable AZURE_RG.
      --network-subscription-id string        Azure Network Subscription ID.
      --network-rg string                     Azure Network Resource Group.
      --hosted-zone-id string                 [Optional] Hosted Zone ID corresponging to Private DNS Zone.
      --region stringArray                    [Required] Region associated with the Azure provider. Minimum number of required regions = 1. Provide the following comma separated fields as key-value pairs:"region-name=<region-name>,vnet=<virtual-network>,sg-id=<security-group-id>,yb-image=<custom-ami>". Region name and Virtual network are required key-values. Security Group ID and YB Image (AMI) are optional. Each region needs to be added using a separate --region flag. Example: --region region-name=westus2,vnet=<vnet-id>
      --zone stringArray                      [Required] Zone associated to the Azure Region defined. Provide the following comma separated fields as key-value pairs:"zone-name=<zone-name>,region-name=<region-name>,subnet=<subnet-id>".Zone name, Region name and subnet IDs are required values. Secondary subnet ID is optional. Each --region definition must have atleast one corresponding --zone definition. Multiple --zone definitions can be provided per region.Each zone needs to be added using a separate --zone flag. Example: --zone zone-name=westus2-1,region-name=westus2,subnet=<subnet-id>
      --ssh-user string                       [Optional] SSH User to access the YugabyteDB nodes. (default "centos")
      --ssh-port int                          [Optional] SSH Port to access the YugabyteDB nodes. (default 22)
      --custom-ssh-keypair-name string        [Optional] Provide custom key pair name to access YugabyteDB nodes. If left empty, YugabyteDB Anywhere will generate key pairs to access YugabyteDB nodes.
      --custom-ssh-keypair-file-path string   [Optional] Provide custom key pair file path to access YugabyteDB nodes. Required with --custom-ssh-keypair-name.
      --airgap-install                        [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads. (default false)
      --ntp-servers stringArray               [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
  -h, --help                                  help for create
```

### Options inherited from parent commands

```
  -a, --apiToken string    YugabyteDB Anywhere api token.
      --config string      Config file, defaults to $HOME/.yba-cli.yaml
      --debug              Use debug mode, same as --logLevel debug.
      --disable-color      Disable colors in output. (default false)
  -H, --host string        YugabyteDB Anywhere Host (default "http://localhost:9000")
  -l, --logLevel string    Select the desired log level format. Allowed values: debug, info, warn, error, fatal. (default "info")
  -n, --name string        [Optional] The name of the provider for the action. Required for create, delete, describe, update.
  -o, --output string      Select the desired output format. Allowed values: table, json, pretty. (default "table")
      --timeout duration   Wait command timeout, example: 5m, 1h. (default 168h0m0s)
      --wait               Wait until the task is completed, otherwise it will exit immediately. (default true)
```

### SEE ALSO

* [yba provider azure](yba_provider_azure.md)	 - Manage a YugabyteDB Anywhere Azure provider


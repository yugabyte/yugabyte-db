## yba provider aws update

Update an AWS YugabyteDB Anywhere provider

### Synopsis

Update an AWS provider in YugabyteDB Anywhere

```
yba provider aws update [flags]
```

### Examples

```
yba provider aws update --name <provider-name> \
	 --remove-region <region-1> --remove-region <region-2>
```

### Options

```
      --new-name string                                 [Optional] Updating provider name.
      --access-key-id string                            [Optional] AWS Access Key ID. Required if provider does not use IAM instance profile. Required with secret-access-key. 
      --secret-access-key string                        [Optional] AWS Secret Access Key. Required if provider does not use IAM instance profile. Required with access-key-id.
      --use-iam-instance-profile                        [Optional] Use IAM Role from the YugabyteDB Anywhere Host.
      --hosted-zone-id string                           [Optional] Updating Hosted Zone ID corresponding to Amazon Route53.
      --add-region stringArray                          [Optional] Add region associated with the AWS provider. Provide the following double colon (::) separated fields as key-value pairs:"region-name=<region-name>::vpc-id=<vpc-id>::sg-id=<security-group-id>". Region name is required key-value. VPC ID and Security Group ID are optional. Each region needs to be added using a separate --add-region flag.
      --add-zone stringArray                            [Optional] Zone associated to the AWS Region defined. Provide the following double colon (::) separated fields as key-value pairs:"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>::secondary-subnet=<secondary-subnet-id>". Zone name, Region name and subnet IDs are required values. Secondary subnet ID is optional. Each --add-region definition must have atleast one corresponding --add-zone definition. Multiple --add-zone definitions can be provided for new and existing regions.Each zone needs to be added using a separate --add-zone flag.
      --remove-region stringArray                       [Optional] Region name to be removed from the provider. Each region to be removed needs to be provided using a separate --remove-region definition. Removing a region removes the corresponding zones.
      --remove-zone stringArray                         [Optional] Remove zone associated to the AWS Region defined. Provide the following double colon (::) separated fields as key-value pairs:"zone-name=<zone-name>::region-name=<region-name>". Zone name, Region name are required values. Each zone needs to be removed using a separate --remove-zone flag.
      --edit-region stringArray                         [Optional] Edit region details associated with the AWS provider. Provide the following double colon (::) separated fields as key-value pairs:"region-name=<region-name>::vpc-id=<vpc-id>::sg-id=<security-group-id>". Region name is a required key-value pair. VPC ID and Security Group ID are optional. Each region needs to be modified using a separate --edit-region flag.
      --edit-zone stringArray                           [Optional] Edit zone associated to the AWS Region defined. Provide the following double colon (::) separated fields as key-value pairs:"zone-name=<zone-name>::region-name=<region-name>::subnet=<subnet-id>::secondary-subnet=<secondary-subnet-id>". Zone name, Region name are required values. Subnet IDs and Secondary subnet ID is optional. Each zone needs to be modified using a separate --edit-zone flag.
      --add-image-bundle stringArray                    [Optional] Add Image bundles associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-name=<image-bundle-name>::arch=<architecture>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::imdsv2=<true/false>::default=<true/false>". Image bundle name, architecture and SSH user are required key-value pairs. The default for SSH Port is 22, IMDSv2 (This should be true if the Image bundle requires Instance Metadata Service v2) is false. Default marks the image bundle as default for the provider. If default is not specified, the bundle will automatically be set as default if no other default bundle exists for the same architecture. Allowed values for architecture are x86_64 and arm64/aarch64. Each image bundle can be added using separate --add-image-bundle flag.
      --add-image-bundle-region-override stringArray    [Optional] Add Image bundle region overrides associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-name=<image-bundle-name>::region-name=<region-name>::machine-image=<machine-image>". Image bundle name and region name are required key-value pairs. Each --image-bundle definition must have atleast one corresponding --image-bundle-region-override definition for every region added. Each override can be added using separate --add-image-bundle-region-override flag.
      --edit-image-bundle stringArray                   [Optional] Edit Image bundles associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-uuid=<image-bundle-uuid>::ssh-user=<ssh-user>::ssh-port=<ssh-port>::imdsv2=<true/false>::default=<true/false>". Image bundle UUID is a required key-value pair. Each image bundle can be edited using separate --edit-image-bundle flag.
      --edit-image-bundle-region-override stringArray   [Optional] Edit overrides of the region associated with the provider. Provide the following double colon (::) separated fields as key-value pairs: "image-bundle-uuid=<image-bundle-uuid>::region-name=<region-name>::machine-image=<machine-image>". Image bundle UUID and region name are required key-value pairs. Each image bundle can be added using separate --edit-image-bundle-region-override flag.
      --remove-image-bundle stringArray                 [Optional] Image bundle UUID to be removed from the provider. Each bundle to be removed needs to be provided using a separate --remove-image-bundle definition. Removing a image bundle removes the corresponding region overrides.
      --airgap-install                                  [Optional] Are YugabyteDB nodes installed in an air-gapped environment, lacking access to the public internet for package downloads. (default false)
      --ntp-servers stringArray                         [Optional] List of NTP Servers. Can be provided as separate flags or as comma-separated values.
  -h, --help                                            help for update
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

* [yba provider aws](yba_provider_aws.md)	 - Manage a YugabyteDB Anywhere AWS provider

